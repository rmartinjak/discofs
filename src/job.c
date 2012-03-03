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
#include <attr/xattr.h>

static int q_find_job(const void *path, const void *job, void *opmask);
static int do_job_rename(struct job *j, int do_remote);

pthread_mutex_t m_instant_pull = PTHREAD_MUTEX_INITIALIZER;

queue job_queue = QUEUE_INIT;
pthread_mutex_t m_job_queue = PTHREAD_MUTEX_INITIALIZER;

#define FREE(p) { free(p); p = NULL; }
void free_job(void *p)
{
    struct job *j = (struct job *)p;

    FREE(j->path);
    FREE(j->sparam1);
    FREE(j->sparam2);
}

void free_job2(void *p)
{
    free_job(p);
    free(p);
}
#undef FREE

/* ====== STORE JOBS IN DB ====== */
int job_store_queue(void)
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
int job(int op, const char *path, jobp_t p1, jobp_t p2, const char *sp1, const char *sp2)
{
    int res;
    struct job *j;

    if (op == JOB_PUSH || op == JOB_PULL) {
        return schedule_pp(path, op);
    }

    j = malloc(sizeof(struct job));
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
                    worker_block();
                    transfer_rename(j->sparam1, 1);
                    remove_lock(j->path, LOCK_TRANSFER);
                    set_lock(j->sparam1, LOCK_TRANSFER);
                    /* execute job and ignore return value */
                    do_job_cache(j);
                    do_job_remote(j);
                    free_job2(j);
                    worker_unblock();
                    return 0;

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

static int q_find_job(const void *path, const void *job, void *opmask)
{
    char *p = (char *)path;
    struct job *j = (struct job *)job;
    int op = *((int *)opmask);

    if (op != JOB_ANY && !(j->op & op))
        return -1;

    return strcmp(p, j->path);
}

int has_job(const char *path, int opmask)
{
    if (q_contains2(&job_queue, path, q_find_job, &opmask))
        return 1;
    return db_has_job(path, opmask);
}

int delete_jobs(const char *path, int opmask)
{
    struct job *j;

    if (has_job(path, opmask)) {
        while ((j = q_dequeue(&job_queue))) {
            if (strcmp(j->path, path) || opmask == JOB_ANY || !(opmask & j->op)) {
                DEBUG("storing job in db\n");
                db_store_job(j);
            }
            free_job2(j);
        }
    }

    return db_delete_jobs(path, opmask);
}

int schedule_job(struct job *j)
{

    switch (j->op) {
        case 0:
            return 0;
        case JOB_PULL:
        case JOB_PUSH:
            j->prio = PRIO_LOW;
            break;
        case JOB_RENAME:
        case JOB_UNLINK:
            j->prio = PRIO_HIGH;
            break;
        default:
            j->prio = PRIO_MID;
    }

    pthread_mutex_lock(&m_job_queue);
    q_enqueue(&job_queue, j);
    pthread_mutex_unlock(&m_job_queue);

    worker_wakeup();
    return 0;
}

/* shortcut for scheduling push(attr)/pull(attr) jobs via macros schedule_push() etc. */
int schedule_pp(const char *path, int op)
{
    struct job *j;

    j = malloc(sizeof(struct job));
    if (!j) {
        errno = ENOMEM;
        return -1;
    }
    JOB_INIT(j);

    j->op = op;
    j->path = strdup(path);
    if (!j->path) {
        free(j);
        return -1;
    }

    if (has_lock(path, LOCK_TRANSFER)) {
        DEBUG("aborting current transfer\n");
        transfer_abort();
    }

    return schedule_job(j);
}

static int do_job_rename(struct job *j, int do_remote)
{
    int res;
    int sync;
    int keep;
    char *from, *to;

    /* do in cache */
    if (!do_remote) {
        from = cache_path(j->path, strlen(j->path));
        to = cache_path(j->sparam1, strlen(j->sparam1));

        db_delete_path(j->sparam1);

        /* store jobs from queue so db_rename_* won't miss them */
        job_store_queue();

        if (is_dir(from)) {
            DEBUG("renaming directory %s -> %s\n", from, to);
            sync_delete_dir(j->sparam1);
            sync_rename_dir(j->path, j->sparam1);
            db_rename_dir(j->path, j->sparam1);
        }
        else {
            DEBUG("renaming file %s -> %s\n", from, to);
            sync_delete_file(j->sparam1);
            sync_rename_file(j->path, j->sparam1);
            db_rename_file(j->path, j->sparam1);
            if (has_lock(j->path, LOCK_OPEN)) {
                remove_lock(j->path, LOCK_OPEN);
                set_lock(j->sparam1, LOCK_OPEN);
            }
        }
    }
    else {
        from = remote_path(j->path, strlen(j->path));
        to = remote_path(j->sparam1, strlen(j->sparam1));

        if (has_lock(j->sparam1, LOCK_TRANSFER)) {
            transfer_abort();
        }
        else if (has_lock(j->path, LOCK_TRANSFER)) {
            transfer_rename(j->sparam1, 1);
        }
        else if ((sync = get_sync(j->sparam1)) == SYNC_NEW || sync == SYNC_MOD) {
            conflict_handle(j, &keep);
            if (keep == CONFLICT_KEEP_REMOTE) {
                char *to_alt;
                if ((to_alt = conflict_path(to)) == NULL) {
                    free(from);
                    free(to);
                    errno = ENOMEM;
                    return -1;
                }
                free(to);
                to = to_alt;
            }
        }
    }

    if (do_remote && is_dir(from)) {
        res = rename(from, to);
        transfer_rename_dir(j->path, j->sparam1);
    }
    else {
        res = rename(from, to);
    }


    free(from);
    free(to);
    return res;
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
            if (do_remote) {
                sync = get_sync(j->path);

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
            res = chmod(path, (mode_t)j->param1);
            break;

        case JOB_CHOWN:
            res = lchown(path, (uid_t)j->param1, (gid_t)j->param2);
            break;

#ifdef HAVE_SETXATTR
        case JOB_SETXATTR:
            res = lsetxattr(path, j->sparam1, j->sparam2, (size_t)j->param1, (int)j->param2);
            break;
#endif
        default:
            errno = EINVAL;
            return -1;
    }

    /* ignore ENOENT error when performing remote job.
       (mode and ownership will be set after transfer is finished */
    if (do_remote && res && errno == ENOENT)
        res = 0;

    if (do_remote && !res && !has_job(j->path, JOB_ANY)) {
        set_sync(j->path);
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

    pr = remote_path(path, p_len);
    pc = cache_path(path, p_len);

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
    set_sync(path);

    pthread_mutex_unlock(&m_instant_pull);
    return 0;
}
