/*! @file
 * worker functions.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "worker.h"

#include "discofs.h"
#include "state.h"
#include "log.h"
#include "funcs.h"
#include "transfer.h"
#include "remoteops.h"
#include "sync.h"
#include "lock.h"
#include "job.h"
#include "conflict.h"
#include "hardlink.h"
#include "bst.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

struct new_hardlink
{
    ino_t inode;
    char *path;
};


static unsigned long long worker_block_n = 0;
static pthread_mutex_t m_worker_block = PTHREAD_MUTEX_INITIALIZER;

static bool worker_wkup = false;
static pthread_mutex_t m_worker_wakeup = PTHREAD_MUTEX_INITIALIZER;

static void worker_scan_remote(void);
static void worker_scan_dir(queue *scan_q, queue *new_hardlink_q);

static int worker_cancel_scan_dir = 0;

/*! SLEEP */
void worker_wakeup(void)
{
    pthread_mutex_lock(&m_worker_wakeup);
    DEBUG("waking up worker thread");
    worker_wkup = true;
    pthread_mutex_unlock(&m_worker_wakeup);
}

void worker_sleep(unsigned int seconds)
{
    pthread_mutex_lock(&m_worker_wakeup);
    worker_wkup = false;
    pthread_mutex_unlock(&m_worker_wakeup);
    while (seconds-- && !worker_wkup && !(EXITING))
    {
        sleep(1);
    }
}


/*! BLOCK */
void worker_block(void)
{
    pthread_mutex_lock(&m_worker_block);
    worker_block_n++;
    pthread_mutex_unlock(&m_worker_block);
}

void worker_unblock(void)
{
    pthread_mutex_lock(&m_worker_block);
    if (worker_block_n)
        worker_block_n--;
    else
        DEBUG("BUG: erroneous call to worker_unblock!");
    pthread_mutex_unlock(&m_worker_block);
}

int worker_blocked(void)
{
    return (worker_block_n != 0);
}

/*! SCAN REMOTE FS */
static void worker_scan_remote(void)
{
    static queue *scan_q = NULL;
    static queue *new_hardlink_q = NULL;
    struct new_hardlink *hl;

    if (!scan_q)
        scan_q = q_init();
    if (!new_hardlink_q)
        new_hardlink_q = q_init();

    /* scan was cancelled -> begin a new scan from root */
    if (worker_cancel_scan_dir)
    {
        /* clear both queues */
        q_clear(scan_q, free);

        while ((hl = q_dequeue(new_hardlink_q)))
        {
            free(hl->path);
            free(hl);
        }
        worker_cancel_scan_dir = 0;
    }

    /* if scan_q is empty, the whole remote directory tree was scanned */
    if (q_empty(scan_q))
    {
        /* create collected new hardlinks */
        while ((hl = q_dequeue(new_hardlink_q)))
        {
            if (hardlink_create(hl->path, hl->inode))
                ERROR("can't create hardlink %s", hl->path);
            free(hl->path);
            free(hl);
        }

        /* sleep and begin new scan */
        worker_sleep(discofs_options.scan_interval);
        VERBOSE("beginning remote scan");
        q_enqueue(scan_q, strdup("/"));
    }

    worker_scan_dir(scan_q, new_hardlink_q);
}

static void worker_scan_dir(queue *scan_q, queue *new_hardlink_q)
{
    int res, sync;
    char *srch;
    char *srch_r;
    char *srch_c;
    size_t srch_len, d_len;
    char *p;
    DIR *dirp;
    size_t dbufsize;
    struct dirent *dbuf;
    struct dirent *ent;
    struct stat st;
    bst *found_tree = bst_init();

    if (!ONLINE)
        return;

    srch = q_dequeue(scan_q);

    srch_len = strlen(srch);
    srch_r = remote_path2(srch, srch_len);
    srch_c = cache_path2(srch, srch_len);

    /* directory not in cache -> create it. */
    if (!is_dir(srch_c))
    {
        clone_dir(srch_r, srch_c);
    }

    dirp = opendir(srch_r);
    if (dirp)
    {
        dbufsize = dirent_buf_size(dirp);
        dbuf = malloc(dbufsize);
    }
    else
        dbuf = NULL;

    if (!dbuf)
    {
        free(srch_r);
        free(srch);
        free(srch_c);
        return;
    }

    while (ONLINE && !worker_cancel_scan_dir && dirp && (res = readdir_r(dirp, dbuf, &ent)) == 0 && ent)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        bst_insert(found_tree, djb2(ent->d_name, SIZE_MAX), NULL);

        d_len = strlen(ent->d_name);

        /* remote path */
        p = join_path2(srch_r, 0, ent->d_name, d_len);
        res = lstat(p, &st);
        free(p);
        if (res == -1)
        {
            DEBUG("lstat in scan_remote failed");
            break;
        }

        /* discofs path */
        p = join_path2(srch, srch_len, ent->d_name, d_len);

        if (S_ISDIR(st.st_mode))
        {
            q_enqueue(scan_q, p);
        }
        else
        {
            sync = sync_get(p);

            if (sync == SYNC_NEW && st.st_nlink >= 2)
            {
                struct new_hardlink *hl = malloc(sizeof *hl);
                if (!hl || !(hl->path = strdup(p)))
                {
                    free(hl);
                    ERROR("memory allocation failed");
                }
                else
                {
                    hl->inode = st.st_ino;
                    q_enqueue(new_hardlink_q, hl);
                }
            }
            else if (sync == SYNC_MOD || sync == SYNC_NEW)
            {
                if (!job_exists(p, JOB_PUSH))
                    job_schedule_pull(p);
                else
                {
                    DEBUG("conflict: sync of target is %s",
                        (sync == SYNC_MOD) ? "SYNC_MOD" : "SYNC_NEW");
                    conflict_handle(p, JOB_PUSH, NULL);
                }
            }

            free(p);
        }
    }
    if (dirp)
        closedir(dirp);
    else
    {
        DEBUG("open dir %s was closed while reading it", srch_r);
        state_set(STATE_OFFLINE, NULL);
    }
    free(dbuf);

    /* READ CACHE DIR to check for remotely deleted files */
    dirp = opendir(srch_c);
    if (dirp)
    {
        dbufsize = dirent_buf_size(dirp);
        dbuf = malloc(dbufsize);
    }
    else
        dbuf = NULL;

    if (!dbuf)
    {
        free(srch_r);
        free(srch);
        free(srch_c);
        return;
    }

    while (ONLINE && !worker_cancel_scan_dir && dirp && (res = readdir_r(dirp, dbuf, &ent)) == 0 && ent)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if (!bst_contains(found_tree, djb2(ent->d_name, SIZE_MAX)))
        {
            p = join_path2(srch, srch_len, ent->d_name, 0);
            if (!p)
                break;

            if (!job_exists(p, LOCK_OPEN) && !job_exists(p, JOB_PUSH))
            {
                VERBOSE("removing missing file %s/%s from cache", (strcmp(srch, "/")) ? srch : "", ent->d_name);
                delete_or_backup(p, CONFLICT_KEEP_REMOTE);
            }
            free(p);
        }
    }

    bst_free(found_tree, NULL);
    free(srch_c);
    free(srch_r);
    free(srch);
    if (dirp)
        closedir(dirp);
    free(dbuf);
}

void worker_cancel_scan(void)
{
    worker_cancel_scan_dir = 1;
}


static int worker_perform(struct job *j)
{
    if (!j)
        return -1;

    switch (j->op)
    {
        case JOB_RENAME:
            return remoteop_rename(j->path, j->s1);
        case JOB_CREATE:
            return remoteop_create(j->path, j->n1, j->n2);
        case JOB_UNLINK:
            return remoteop_unlink(j->path);
        case JOB_SYMLINK:
            return remoteop_symlink(j->s1, j->path);
        case JOB_LINK:
            return remoteop_link(j->s1, j->path);
        case JOB_MKDIR:
            return remoteop_mkdir(j->path, j->n1);
        case JOB_RMDIR:
            return remoteop_rmdir(j->path);
        case JOB_CHOWN:
            return remoteop_chown(j->path, j->n1, j->n2);
        case JOB_CHMOD:
            return remoteop_chmod(j->path, j->n1);
#if HAVE_SETXATTR
        case JOB_SETXATTR:
            return remoteop_setxattr(j->path, j->s1, j->s2, j->n1, j->n2);
#endif
    }

    return -1;
}

/*! WORKER THREAD */
void *worker_main(void *arg)
{
    int res;
    struct job *j = NULL;

    while (!EXITING)
    {
        /* flush job scheduling queue to db */
        job_store();

        /* flush sync change queue to db */
        sync_store();

        if (ONLINE)
        {
            /* sleep if blocked */
            if (worker_blocked())
            {
                worker_sleep(SLEEP_LONG);
                continue;
            }

            /* if a transfer job is in progress, try resume it */
            if (j)
            {
                res = transfer(NULL, NULL);

                /* everything OK -> next iteration of main loop */
                if (res == TRANSFER_OK)
                    continue;

                /* transfer finished or error */
                lock_remove(j->path, LOCK_TRANSFER);
                job_return(j, (res == TRANSFER_FINISH) ? JOB_DONE : JOB_FAILED);
                j = NULL;
            }


            /*---------------*/
            /* get a new job */
            /*---------------*/

            j = job_get();

            /* skip locked files */
            while (j && (j->op & (JOB_PUSH|JOB_PULL)) && lock_has(j->path, LOCK_OPEN))
            {
                DEBUG("%s is locked, NEXT", j->path);
                job_return(j, JOB_LOCKED);
                j = job_get();
            }

            /* no jobs -> scan remote fs for changes*/
            if (!j)
            {
                worker_scan_remote();
                continue;
            }

            if (j->op == JOB_PUSH || j->op == JOB_PULL)
            {
                /* check PUSH job for conflict */
                if (j->op == JOB_PUSH)
                {
                    int sync = sync_get(j->path);
                    if (sync == SYNC_MOD || sync == SYNC_NEW)
                    {
                        DEBUG("conflict: sync of target is %s",
                            (sync == SYNC_MOD) ? "SYNC_MOD" : "SYNC_NEW");
                        conflict_handle(j->path, j->op, NULL);
                        job_return(j, JOB_DONE);
                        j = NULL;
                        continue;
                    }
                }

                VERBOSE("beginning %s on %s", job_opstr(j->op), j->path);
                res = transfer_begin(j);

                if (res == TRANSFER_FINISH)
                {
                    job_return(j, JOB_DONE);
                    j = NULL;
                }
                else if (res == TRANSFER_FAIL)
                {
                    ERROR("transfering '%s' failed", j->path);
                    job_return(j, JOB_FAILED);
                    j = NULL;
                }

                /* if result is TRANSFER_OK, keep j */
            }
            /* neither PUSH nor PULL */
            else
            {
                VERBOSE("performing %s on %s", job_opstr(j->op), j->path);
                res = worker_perform(j);
                job_return(j, (!res) ? JOB_DONE : JOB_FAILED);
                j = NULL;
            }
        }

        /* OFFLINE */
        else
        {
            worker_sleep(SLEEP_LONG);
        }

    }

    VERBOSE("exiting job thread");
    if (j)
    {
        lock_remove(j->path, LOCK_TRANSFER);
        transfer_abort();
        job_return(j, JOB_LOCKED);
        j = NULL;
    }
return NULL;
}
