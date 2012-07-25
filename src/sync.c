/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "sync.h"

#include "discofs.h"
#include "state.h"
#include "log.h"
#include "funcs.h"
#include "hashtable.h"
#include "hardlink.h"
#include "db.h"

#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>

/*=============*/
/* DEFINITIONS */
/*=============*/

/* queue for changed sync entries + mutex */
static queue *sync_queue;
static pthread_mutex_t m_sync_queue = PTHREAD_MUTEX_INITIALIZER;

/* yo dawg i herd u like fast lookups so we put some hashtables inside a
   hashtable so you can have O(1) lookup time while you have O(1) lookup time

   sync_ht is a hashtable (we'll call it "dirname ht") which uses
   key:     dirname (for "/foo/bar": /foo)
   value:   a hashtable ("basename ht") using
            key:    basename (for "/foo/bar": bar)
            value:  sync data
    so to get/set sync time, we first need to lookup/create the dirname
    hashtable and then get/set the entry for basename
 */
static hashtable *sync_ht = NULL;
static pthread_mutex_t m_sync_ht = PTHREAD_MUTEX_INITIALIZER;


/*-------------------*/
/* static prototypes */
/*-------------------*/

/* hash function for hashtables */
static hash_t sync_hash(const void *p, const void *n);

/* compare function for hashtables */
static int sync_cmp(const void *p1, const void *p2, const void *n);

/* free dirname ht + contained basename hts */
static void sync_ht_free(void);

/* set sync entry */
static struct sync *sync_ht_set(const char *path, sync_xtime_t mtime, sync_xtime_t ctime);

/* retrieve sync entry */
static int sync_ht_get(const char *path, struct sync *s);


/*==================*/
/* STATIC FUNCTIONS */
/*==================*/

static hash_t sync_hash(const void *p, const void *n)
{
    if (n)
        return djb2((const char*)p, *((size_t*)n));
    else
        return djb2((const char*)p, SIZE_MAX);
}

static int sync_cmp(const void *p1, const void *p2, const void *n)
{
    if (n)
        return strncmp((char*)p1, (char*)p2, *((size_t*)n));
    else
        return strcmp((char*)p1, (char*)p2);
}

static void sync_ht_free(void)
{
    htiter *it;
    hashtable *ht;
    char *p;

    /* sync_ht must be initialized */
    if (!sync_ht)
        return;

    pthread_mutex_lock(&m_sync_ht);

    /* create iterator */
    it = ht_iter(sync_ht);

    /* free all items of dirname hashtable */
    while (htiter_next(it, (void**)&p, (void**)&ht))
    {
        /* free basename ht */
        ht_free_f(ht, NULL, sync_free);

        /* free path */
        free(p);
    }

    /* free iterator object */
    free(it);

    /* finally free the dirname hashtable */
    ht_free(sync_ht);
    sync_ht = NULL;

    pthread_mutex_unlock(&m_sync_ht);
}

static struct sync *sync_ht_set(const char *path, sync_xtime_t mtime, sync_xtime_t ctime)
{
    struct sync *s = NULL;
    hashtable *ht;
    char *dir, *base;
    size_t n;

    /* the first character of path must be a '/' */
    if (*path != '/')
        return NULL;

    /* determine length of dirname (without trailing '/') */
    n = strrchr(path, '/') - path;

    /* get basename ht (or create if it doesn't exist) */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL)
    {

        /* hash table for dir part of path not found -> create it */
        if (ht_init(&ht, sync_hash, sync_cmp) == HT_ERROR)
            return NULL;

        /* copy dirname */
        dir = malloc(n+1);
        if (!dir)
        {
            errno = ENOMEM;
            return NULL;
        }
        dir[n] = '\0';
        memcpy(dir, path, n);

        if (ht_insert(sync_ht, dir, ht) == HT_ERROR)
        {
            ERROR("inserting into sync_ht\n");
            free(dir);
            return NULL;
        }

        s = NULL;
    }
    else
    {
        /* find sync item */
        s = ht_get(ht, path + n + 1);
    }

    if (!s)
    {
        s = sync_create(path, mtime, ctime);
        /* create new path & sync item */
        if (!s)
        {
            errno = ENOMEM;
            return NULL;
        }

        base = strrchr(s->path, '/') + 1;

        if (ht_insert(ht, base, s) != HT_OK)
        {
            ERROR("error inserting into sync_ht\n");
            sync_free(s);
            return NULL;
        }
    }

    /* set data of found or created sync */
    s->mtime = mtime;
    s->ctime = ctime;
    return s;
}

static int sync_ht_get(const char *path, struct sync *s)
{
    hashtable *ht;
    size_t n;
    struct sync *found;

    n = strrchr(path, '/') - path;

    /* find ht according to dirname */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL)
    {
        return -1;
    }

    /* get sync entry for filename */
    found = ht_get(ht, path + n + 1);

    if (found)
    {
        s->mtime = found->mtime;
        s->ctime = found->ctime;
        return 0;
    }

    return -1;
}


/*====================*/
/* EXPORTED FUNCTIONS */
/*====================*/

int sync_timecmp(sync_xtime_t t1, sync_xtime_t t2)
{
#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    if (t1.tv_sec == t2.tv_sec)
    {
        if (!(discofs_options.fs_features & FEAT_NS))
            return 0;
        else
            return t1.tv_nsec - t2.tv_nsec;
    }
    else
        return t1.tv_sec - t2.tv_sec;
#else
    return t1 - t2;
#endif
}

int sync_init(void)
{
    int res;

    sync_queue = q_init();
    if (!sync_queue)
        return -1;

    pthread_mutex_lock(&m_sync_ht);

    /* initialize dirname ht */
    ht_init(&sync_ht, sync_hash, sync_cmp);

    /* load sync data from db, call sync_ht_set() for each row */
    res = db_load_sync(sync_ht_set);

    pthread_mutex_unlock(&m_sync_ht);
    return res;
}

int sync_destroy(void)
{
    int res;

    /* store queue to db */
    res = sync_store();
    if (res)
        return res;

    /* free hash table */
    sync_ht_free();

    return 0;
}

int sync_store(void)
{
    struct sync *s;     /* sync data retrieved from queue */
    int res = DB_OK;    /* return value of db_store_sync() */

    do {
        /* dequeue data */
        pthread_mutex_lock(&m_sync_queue);
        s = q_dequeue(sync_queue);
        pthread_mutex_unlock(&m_sync_queue);

        /* store data in db */
        if (s)
            res = db_store_sync(s);

    /* continue if queue wasn't empty and inserting was OK */
    }
    while (s && res == DB_OK);

    /* return error if inserting failed */
    if (res != DB_OK)
        return -1;

    return 0;
}

struct sync *sync_create(const char *path, sync_xtime_t mtime, sync_xtime_t ctime)
{
    struct sync *s = malloc(sizeof *s);

    if (s)
    {
        s->path = strdup(path);

        if (!s->path)
        {
            free(s);
            return NULL;
        }
        s->mtime = mtime;
        s->ctime = ctime;
    }

    return s;
}

void sync_free(void *p)
{
    free(p);
}

int sync_set(const char *path, int flags)
{
    int res;
    char *p;                    /* path to remote file/dir */
    sync_xtime_t mtime, ctime;  /* mtime and ctime of remote file/dir to set */
    struct stat st;             /* stat buffer */
    struct sync *s;             /* sync data to enqueue */

    /* only set sync when online */
    if (!ONLINE)
        return -1;

    /* retrieve mtime and ctime */
    p = remote_path(path);
    res = lstat(p, &st);
    free(p);

    if (res)
    {
        PERROR("lstat() in sync_set");
        return -1;
    }

    if (st.st_nlink > 1 && !(flags & SYNC_NOHARDLINKS) && (discofs_options.fs_features & FEAT_HARDLINKS))
    {
        /* hardlink_sync_set will set sync for all paths sharing the same
           inode, including "path" */
        return hardlink_sync_set(st.st_ino);
    }

    mtime = ST_MTIME(st);
    ctime = ST_CTIME(st);

    /* insert in sync ht */
    pthread_mutex_lock(&m_sync_ht);

    VERBOSE("setting sync for %s\n", path);
    s = sync_ht_set(path, mtime, ctime);

    pthread_mutex_unlock(&m_sync_ht);

    /* if ht entry set successfully, add item to update queue */
    if (s)
    {
        pthread_mutex_lock(&m_sync_queue);

        q_enqueue(sync_queue, s);

        pthread_mutex_unlock(&m_sync_queue);
    }
    /* or log error and return -1 */
    else
    {
        ERROR("sync_set failed\n");
        return -1;
    }

    return 0;
}

int sync_get_stat(const char *path, struct stat *buf)
{
    /* sync state mask, consists of SYNC_ flags */
    int sync;
    int res;
    char *p;
    struct stat st;
    struct sync s;

    if ((p = remote_path(path)) == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    res = lstat(p, &st);
    free(p);

    /* no such file */
    if (res == -1)
    {
        return SYNC_NOT_FOUND;
    }

    /* copy stat data to caller-provided buffer */
    if (buf)
        memcpy(buf, &st, sizeof st);

    /* get sync data from ht */
    pthread_mutex_lock(&m_sync_ht);
    res = sync_ht_get(path, &s);
    pthread_mutex_unlock(&m_sync_ht);

    /* no sync data yet -> new file/dir */
    if (res != 0)
    {
        return SYNC_NEW;
    }

    /* assume SYNC */
    sync = SYNC_SYNC;

    /* not a dir and mtime is newer than in sync ht -> file was modified */
    if (!S_ISDIR(st.st_mode) && sync_timecmp(ST_MTIME(st), s.mtime) > 0)
        sync = SYNC_MOD;
    /* ctime newer -> file/dir was changed */
    else if (sync_timecmp(ST_CTIME(st), s.ctime) > 0)
        sync = SYNC_CHG;

    return sync;
}


int sync_rename_dir(const char *from, const char *to)
{
    hashtable *ht;
    htiter *it;
    char *p, *oldpath, *newpath;
    size_t from_len, to_len;       /* string lengths */
    queue *q = q_init();           /* queue for hashtables to "rename" */

    sync_store();

    from_len = strlen(from);
    to_len = strlen(to);

    pthread_mutex_lock(&m_sync_ht);

    it = ht_iter(sync_ht);
    if (!it)
    {
        pthread_mutex_unlock(&m_sync_ht);
        return -1;
    }

    /* find all basename hashtables where key is a directory below "path" */
    while (htiter_next(it, (void**)&p, NULL))
    {

        /* enqueue key if key is below "path" ? */
        if (strncmp(p, from, from_len))
        {

            /* create copy of the key */
            oldpath = strdup(p);
            if (!oldpath)
            {
                pthread_mutex_unlock(&m_sync_ht);
                q_clear(q, free);
                free(it);
                errno = ENOMEM;
                return -1;
            }
            /* enqueue the copy */
            q_enqueue(q, oldpath);
        }
    }

    free(it);

    /* dequeue found keys, remove corresp onding ht and reinsert with renamed key */
    while ((oldpath = q_dequeue(q)))
    {
        /* generate new key */
        newpath = join_path2(to, to_len, oldpath+from_len, 0);
        if (!newpath)
        {
            free(oldpath);
            errno = ENOMEM;
            break;
        }

        /* remove basename ht */
        ht = ht_remove_f(sync_ht, oldpath, free);

        /* insert basename ht with new key */
        ht_insert(sync_ht, newpath, ht);

        free(oldpath);
    }

    pthread_mutex_unlock(&m_sync_ht);

    /* if q is not empty something terrible happened! */
    if (!q_empty(q))
    {
        q_clear(q, free);
        return -1;
    }

    if (db_sync_rename_dir(from, to) != DB_OK)
        return -1;

    return 0;
}

int sync_rename_file(const char *from, const char *to)
{
    hashtable *ht;
    char *newpath, *base;
    struct sync *s;
    size_t n;

    sync_store();

    n = strrchr(from, '/') - from;

    /* find ht according to dirname */
    if ((ht = ht_get_a(sync_ht, from, &n, &n)) == NULL)
    {
        return -1;
    }

    /* new path */
    newpath = strdup(to);
    if (!newpath)
    {
        return -1;
    }


    /* remove basename ht entry */
    s = ht_remove(ht, from + n + 1);
    if (!s)
    {
        free(newpath);
        return -1;
    }

    /* set new path */
    free(s->path);
    s->path = newpath;

    base = strrchr(s->path, '/') + 1;

    /* reinsert with new key */
    if (ht_insert(ht, base, s))
        return -1;

    if (db_sync_rename_file(from, to) != DB_OK)
        return -1;

    return 0;
}

int sync_delete_dir(const char *path)
{
    hashtable *ht;

    sync_store();

    pthread_mutex_lock(&m_sync_ht);

    /* remove basename ht from dirname ht */
    ht = ht_remove_f(sync_ht, path, free);

    pthread_mutex_unlock(&m_sync_ht);

    /* no hashtable found */
    if (!ht)
        return -1;

    /* ht should be empty (rmdir is only allowed on empty dirs)
       if it isn't, nag a little and free it's content */
    if (!ht_empty(ht))
    {
        ERROR("deleting non-empty dir hashtable\n");
        ht_free_f(ht, NULL, sync_free);
    }

    if (db_sync_delete_path(path) != DB_OK)
        return -1;

    return 0;
}

int sync_delete_file(const char *path)
{
    hashtable *ht;
    struct sync *s;
    size_t n;

    sync_store();

    n = strrchr(path, '/') - path;

    /* find basename ht */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL)
    {
        return -1;
    }

    /* remove sync entry (path+n+1 points to basename part) */
    s = ht_remove(ht, path + n + 1);
    if (!s)
        return -1;

    /* free it */
    sync_free(s);

    if (db_sync_delete_path(path) != DB_OK)
        return -1;

    return 0;
}
