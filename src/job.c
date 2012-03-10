/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "job.h"

#include "fs2go.h"
#include "log.h"
#include "funcs.h"
#include "queue.h"
#include "sync.h"
#include "db.h"
#include "lock.h"
#include "worker.h"
#include "transfer.h"
#include "conflict.h"

#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif

extern struct options fs2go_options;

static int q_find_job(const void *path, const void *job, void *opmask);
static int do_job_rename(struct job *j, int do_remote);

pthread_mutex_t m_instant_pull = PTHREAD_MUTEX_INITIALIZER;

static queue q_low = QUEUE_INIT;
static queue q_mid = QUEUE_INIT;
static queue q_high = QUEUE_INIT;

pthread_mutex_t m_q_low = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_q_mid = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_q_high = PTHREAD_MUTEX_INITIALIZER;

void free_job(void *p)
{
    struct job *j = (struct job *)p;

#define FREE(p) { free(p); p = NULL; }
    FREE(j->path);
    FREE(j->sparam1);
    FREE(j->sparam2);
#undef FREE
}

void free_job2(void *p)
{
    free_job(p);
    free(p);
}

/* ====== STORE JOBS IN DB ====== */
int job_store(void)
{
    struct job *j;
    int res = DB_OK;

    while (res == DB_OK && !q_empty(&job_queue)) {
        pthread_mutex_lock(&m_job_queue);
        j = q_dequeue(&job_queue);
        pthread_mutex_unlock(&m_job_queue);
        res = db_store_job(j);
        free_job2(j);
    }
    if (res != DB_OK) {
        PERROR("storing job in db");
        return -1;
    }

    return 0;
}


/* may not be called with JOB_PUSH or JOB_PULL, these should be scheduled */
int job(int op, const char *path, job_param p1, jobp_t p2, const char *sp1, const char *sp2)
{
    int res;
    struct job *j;

    if (op == JOB_PUSH || op == JOB_PULL) {
        return schedule_pp(path, op);
    }

    j = malloc(sizeof (struct job));
    if (!j) {
        errno = ENOMEM;
        return -1;
    }
    JOB_INIT(j);

    j->op = op;
    j->param1 = p1;
    j->param2 = p2;

    if (!path || strcmp(path, "") == 0)
        FATAL("calling job() without path?\n");

    STRDUP(j->path, path);

    if (sp1) {
        STRDUP(j->sparam1, sp1);
    }
    if (sp2) {
        STRDUP(j->sparam2, sp2);
    }

    if (ONLINE) {
        /* if the target is currently being transferred, rename and unlink must be treated differently */
        if (has_lock(j->path, LOCK_TRANSFER)) {
            switch (j->op) {
                case JOB_RENAME:
                    /* change transfer destination */

                case JOB_UNLINK:
                    /* abort transfer */
                    transfer_abort();
                    remove_lock(j->path, LOCK_TRANSFER);
                    delete_jobs(j->path, JOB_PUSH | JOB_PULL);
                    /* execute job and ignore return value */
                    do_job_cache(j);
                    do_job_remote(j);
                    free_job2(j);
                    return 0;
            }
        }
        res = do_job_cache(j);
        res = do_job_remote(j);
        free_job2(j);
    }
    else {
        res = do_job_cache(j);
        res = schedule_job(j);
    }

    return res;
}

int job_do_unlink(const char *path)
{
    int res, sync;
    char *pr;

    pr = remote_path(path, strlen(path));
            if (do_remote) {
                sync = sync_get(j->path);

                /* this would be a conflict! don't delete but pull */
                if (sync == SYNC_MOD) {
                    schedule_pull(j->path);
                    return 0;
                }
                else if (sync == SYNC_NOT_FOUND) {
                    db_delete_path(j->path);
                    return 0;
                }
                else {
                    db_delete_path(j->path);
                    res = unlink(path);
                    delete_jobs(j->path, JOB_PULL);
                    free(path);
                    return res;
                }
            }
            else {
                res = unlink(path);
                delete_jobs(j->path, JOB_PUSH);
            }
}

int do_job(struct job *j, int do_remote)
{
    int res;
    char *path;
    int sync;

    path = (do_remote) ? remote_path(j->path, strlen(j->path)) : cache_path(j->path, strlen(j->path));

    switch (j->op) {
        case JOB_RENAME:
            if (!j->sparam1)
                return -1;
            res = do_job_rename(j, do_remote);
            break;

        case JOB_UNLINK:
            break;

        case JOB_SYMLINK:
            res = symlink(j->sparam1, path);
            break;

        case JOB_MKDIR:
            res = mkdir(path, (mode_t)j->param1);
            break;

        case JOB_RMDIR:
            res = rmdir(path);
            break;

        case JOB_CHMOD:
            if (!do_remote || !(fs2go_options.copyattr & COPYATTR_NO_MODE))
                res = chmod(path, (mode_t)j->param1);
            else
                res = 0;
            break;

        case JOB_CHOWN:

    }

    /* ignore ENOENT error when performing remote job.
       (mode and ownership will be set after transfer is finished */
    if (do_remote && res && errno == ENOENT)
        res = 0;

    if (do_remote && !res && !has_job(j->path, JOB_ANY)) {
        sync_set(j->path);
    }

    free(path);
    return res;
}

/* instantly copy a file from remote to cache */
int instant_pull(const char *path)
{
    int res;
    char *pc;
    char *pr;
    size_t p_len = strlen(path);

    VERBOSE("instant_pulling %s\n", path);

    pthread_mutex_lock(&m_instant_pull);

    worker_block();

    pr = remote_path2(path, p_len);
    pc = cache_path2(path, p_len);

    /* copy data */
    res = copy_file(pr, pc);

    worker_unblock();

    copy_attrs(pr, pc);
    free(pr);
    free(pc);

    /* if copying failed, return error and dont set sync */
    if (res == -1) {
        ERROR("instant_pull on %s FAILED\n", path);
        pthread_mutex_unlock(&m_instant_pull);
        return -1;
    }

    /* file is in sync now */
    delete_jobs(path, JOB_PULL|JOB_PULLATTR);
    sync_set(path);

    pthread_mutex_unlock(&m_instant_pull);
    return 0;
}
