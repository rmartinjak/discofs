/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "sync.h"

#include "fs2go.h"
#include "log.h"
#include "funcs.h"
#include "job.h"
#include "hashtable.h"
#include "db.h"

#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>

extern struct options fs2go_options;

queue sync_queue = QUEUE_INIT;
pthread_mutex_t m_sync_queue = PTHREAD_MUTEX_INITIALIZER;

hashtable *sync_ht = NULL;
pthread_mutex_t m_sync_ht = PTHREAD_MUTEX_INITIALIZER;

static void sync_free2(void *p);

void sync_free(struct sync *s)
{
    if (s)
        free(s->path);
}

static void sync_free2(void *p)
{
    sync_free((struct sync*) p);
    free(p);
}

int set_sync(const char *path)
{
    char *p;
    sync_xtime_t mtime, ctime;
    struct stat st;
    struct sync *s;

    /* can only set sync when online */
    if (!ONLINE)
        return -1;

    GETTIME(mtime);

    p = remote_path(path, strlen(path));
    /* get ctime */
    if (lstat(p, &st) == -1) {
        free(p);
        return -1;
    }
    ctime = ST_CTIME(st);
    free(p);

    /* db_set_sync(path, &s); */
    pthread_mutex_lock(&m_sync_ht);
    VERBOSE("setting sync for %s\n", path);
    s = sync_ht_set(path, mtime, ctime);
    pthread_mutex_unlock(&m_sync_ht);

    if (s) {
        pthread_mutex_lock(&m_sync_queue);
        q_enqueue(&sync_queue, s);
        pthread_mutex_unlock(&m_sync_queue);
        return 0;
    }
    ERROR("sync_ht_set failed!?\n");

    return -1;
}

int get_sync_stat(const char *path, struct stat *buf)
{
    int sync;
    int res;
    char *p;
    struct stat st;
    struct sync s;

    if ((p = remote_path(path, strlen(path))) == NULL) {
        errno = ENOMEM;
        return -1;
    }

    res = lstat(p, &st);
    free(p);
    /* no such file */
    if (res == -1) {
        DEBUG("file not found remote: %s\n", path);
        return SYNC_NOT_FOUND;
    }

    pthread_mutex_lock(&m_sync_ht);
    res = sync_ht_get(path, &s);
    pthread_mutex_unlock(&m_sync_ht);
    if (res != 0) {
        if (buf)
            memcpy(buf, &st, sizeof(struct stat));
        return SYNC_NEW;
    }

    sync = SYNC_SYNC;

    if (!S_ISDIR(st.st_mode) && timecmp(ST_MTIME(st), s.mtime) > 0)
        sync = SYNC_MOD;
    else if (timecmp(ST_CTIME(st), s.ctime) > 0)
        sync = SYNC_CHG;

    if (buf && sync & (SYNC_CHG|SYNC_MOD))
        memcpy(buf, &st, sizeof(struct stat));

    return sync;
}

static hash_t sync_hash(const void *p, const void *n)
{
    if (n)
        return (hash_t)djb2((char*)p, *((size_t*)n));
    else
        return (hash_t)djb2((char*)p, -1);
}

static int sync_cmp(const void *p1, const void *p2, const void *n)
{
    if (n)
        return strncmp((char*)p1, (char*)p2, *((size_t*)n));
    else
        return strcmp((char*)p1, (char*)p2);
}

int sync_load()
{
    int res;

    pthread_mutex_lock(&m_sync_ht);
    ht_init(&sync_ht, sync_hash, sync_cmp);

    res = db_load_sync();

    pthread_mutex_unlock(&m_sync_ht);
    return res;
}

int sync_store()
{
    struct sync *s;
    int res = DB_OK;

    do {
        pthread_mutex_lock(&m_sync_queue);
        s = q_dequeue(&sync_queue);
        pthread_mutex_unlock(&m_sync_queue);

        if (s)
            res = db_store_sync(s);

    } while (s && res == DB_OK);

    if (res != DB_OK)
        return -1;

    return 0;
}

void sync_ht_free(void)
{
    htiter *it;
    hashtable *ht;
    char *p;
    if (!sync_ht)
        return;
    pthread_mutex_lock(&m_sync_ht);

    it = ht_iter(sync_ht);

    while (htiter_next(it, (void**)&p, (void**)&ht)) {
        ht_free_f(ht, NULL, sync_free2);
        free(p);
    }
    free(it);
    ht_free(sync_ht);
    pthread_mutex_unlock(&m_sync_ht);
}

struct sync *sync_ht_set(const char *path, sync_xtime_t mtime, sync_xtime_t ctime)
{
    struct sync *s = NULL;
    hashtable *ht;
    char *dir;
    char *file;
    size_t n;

    /* we can safely assume that path always contains a / */
    n = strrchr(path, '/') - path;

    /* yo dawg i heard you like hashtables */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL) {

        /* hash table for dir part of path not found -> create it */
        if (ht_init(&ht, sync_hash, sync_cmp) == HT_ERROR)
            return NULL;

        dir = malloc(n+1);
        if (!dir) {
            errno = ENOMEM; return NULL;
        }

        dir[n] = '\0';
        memcpy(dir, path, n);

        if (ht_insert(sync_ht, dir, ht) == HT_ERROR)
            PERROR("inserting ht to sync_ht");

        s = NULL;
    }
    else {
        /* find item */
        s = ht_get(ht, path + n + 1);
    }

    if (!s) {
        /* create new path & sync item */
        if ((s = malloc(sizeof(struct sync))) == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        if ((s->path = strdup(path)) == NULL) {
            free(s);
            errno = ENOMEM;
            return NULL;
        }
        if ((file = basename_r(path)) == NULL) {
            free(s->path);
            free(s);
            errno = ENOMEM;
            return NULL;
        }
        if (ht_insert(ht, file, s) != HT_OK) {
            PERROR("inserting ht to sync_ht");
            free(file);
            free(s->path);
            free(s);
            return NULL;
        }
    }

    /* set data of found or created sync */
    s->mtime = mtime;
    s->ctime = ctime;
    return s;
}

int sync_ht_get(const char *path, struct sync *s)
{
    hashtable *ht;
    size_t n;
    struct sync *found;

    n = strrchr(path, '/') - path;

    /* find ht according to dirname */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL) {
        return -1;
    }

    /* get sync entry for filename */
    found = ht_get(ht, path + n + 1);

    if (found) {
        s->mtime = found->mtime;
        s->ctime = found->ctime;
        return 0;
    }

    return -1;
}


int sync_rename_dir(const char *from, const char *to)
{
    queue q = QUEUE_INIT;
    hashtable *ht;
    htiter *it;
    char *p, *oldpath, *newpath;
    size_t from_len, to_len;

    from_len = strlen(from);
    to_len = strlen(to);

    it = ht_iter(sync_ht);
    if (!it)
        return -1;

    /* find all directory hashtables where path is below "path" */
    pthread_mutex_lock(&m_sync_ht);
    while (htiter_next(it, (void**)&p, NULL)) {
        if (strncmp(p, from, from_len)) {
            if ((oldpath = strdup(p)) == NULL) {
                pthread_mutex_unlock(&m_sync_ht);
                q_clear(&q, 1);
                free(it);
                errno = ENOMEM;
                return -1;
            }
            q_enqueue(&q, oldpath);
        }
    }

    while ( /* items in queue ? */
            (oldpath = q_dequeue(&q))
            /* retrieve and remove directory hashtable from "master" ht */
            && (ht = ht_remove(sync_ht, oldpath))
            /* create new path */
            && (newpath = join_path(to, to_len, oldpath+from_len, strlen(oldpath+from_len)))) {
        free(oldpath);
        ht_insert(sync_ht, newpath, ht);
    }

    free(it);
    pthread_mutex_unlock(&m_sync_ht);

    if (!q_empty(&q)) {
        q_clear(&q, 1);
        return -1;
    }
    return 0;
}

/* dirs can only be removed if they're empty -> path will only be matched exactly, not partially */
int sync_delete_dir(const char *path)
{
    hashtable *ht;

    pthread_mutex_lock(&m_sync_ht);

    ht = ht_remove_f(sync_ht, path, free);

    pthread_mutex_unlock(&m_sync_ht);

    if (!ht)
        return -1;

    if (!ht_empty(ht))
        ERROR("deleting non-empty dir hashtable\n");

    ht_free_f(ht, NULL, sync_free2);
    free(ht);
    return 0;
}

int sync_delete_file(const char *path)
{
    hashtable *ht;
    struct sync *s;
    size_t n;

    n = strrchr(path, '/') - path;

    /* find ht according to dirname */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL) {
        return -1;
    }

    s = ht_remove(ht, path + n +1);
    if (!s)
        return -1;
    sync_free2(s);
    return 0;
}

int sync_rename_file(const char *path, const char *newpath)
{
    hashtable *ht;
    struct sync *s;
    size_t n;

    n = strrchr(path, '/') - path;

    /* find ht according to dirname */
    if ((ht = ht_get_a(sync_ht, path, &n, &n)) == NULL) {
        return -1;
    }

    /* get sync entry for filename */
    s = ht_remove(ht, path + n +1);
    if (!s)
        return -1;

    free(s->path);
    if ((s->path = dirname_r(newpath)) == NULL) {
        return -1;
    }
    return ht_insert(ht, s->path, s);
}
