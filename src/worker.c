/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "worker.h"

#include "fs2go.h"
#include "log.h"
#include "funcs.h"
#include "transfer.h"
#include "sync.h"
#include "lock.h"
#include "job.h"
#include "db.h"
#include "conflict.h"
#include "bst.h"

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#if HAVE_CLOCK_GETTIME
#include <sys/time.h>
#else
#include <time.h>
#endif

static unsigned long long worker_block_n = 0;
static pthread_mutex_t m_worker_block = PTHREAD_MUTEX_INITIALIZER;

static int worker_wkup = 0;
static pthread_mutex_t m_worker_wakeup = PTHREAD_MUTEX_INITIALIZER;

/* from fs2go.c */
extern struct options fs2go_options;


/* ====== SLEEP ====== */
void worker_wakeup()
{
    pthread_mutex_lock(&m_worker_wakeup);
    DEBUG("waking up worker thread\n");
    worker_wkup = 1;
    pthread_mutex_unlock(&m_worker_wakeup);
}

void worker_sleep(unsigned int seconds)
{
    pthread_mutex_lock(&m_worker_wakeup);
    worker_wkup = 0;
    pthread_mutex_unlock(&m_worker_wakeup);
    while (seconds-- && !worker_wkup && !(EXITING)) {
        sleep(1);
    }
}


/* ====== BLOCK ====== */
void worker_block()
{
    pthread_mutex_lock(&m_worker_block);
    worker_block_n++;
    pthread_mutex_unlock(&m_worker_block);
}

void worker_unblock()
{
    pthread_mutex_lock(&m_worker_block);
    worker_block_n--;
    pthread_mutex_unlock(&m_worker_block);
}

int worker_has_block()
{
    return (worker_block_n != 0);
}

/* ====== SCAN REMOTE FS ====== */
void scan_remote(queue *q)
{
    int res;
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
    bst *found_tree = bst_init(NULL);

    if (!ONLINE)
        return;

    srch = q_dequeue(q);
    if (!srch) {
        VERBOSE("beginning remote scan\n");
        srch = "/";
    }

    srch_len = strlen(srch);
    srch_r = remote_path2(srch, srch_len);
    srch_c = cache_path2(srch, srch_len);

    /* directory not in cache -> create it. */
    if (!is_dir(srch_c)) {
        clone_dir(srch_r, srch_c);
    }

    dirp = opendir(srch_r);
    if (dirp) {
        dbufsize = dirent_buf_size(dirp);
        dbuf = malloc(dbufsize);
    }
    else
        dbuf = NULL;

    if (!dbuf) {
        free(srch_r);
        free(srch);
        free(srch_c);
        return;
    }

    while (ONLINE && dirp && (res = readdir_r(dirp, dbuf, &ent)) == 0 && ent) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        bst_insert(found_tree, djb2(ent->d_name, SIZE_MAX), NULL);

        d_len = strlen(ent->d_name);

        p = join_path2(srch_r, 0, ent->d_name, d_len);
        res = lstat(p, &st);
        free(p);
        if (res == -1) {
            DEBUG("lstat in scan_remote failed\n");
            break;
        }
        p = join_path2(srch, srch_len, ent->d_name, d_len);

        if (S_ISDIR(st.st_mode)) {
            q_enqueue(q, p);
        }
        else {
            switch (sync_get(p)) {
                case SYNC_NEW:
                case SYNC_MOD:
                    schedule_pull(p);
                    break;
                case SYNC_CHG:
                    schedule_pullattr(p);
                    break;
            }
            free(p);
        }
    }
    if (dirp)
        closedir(dirp);
    else {
        DEBUG("open dir %s was closed while reading it\n", srch_r);
        set_state(STATE_OFFLINE, NULL);
    }
    free(dbuf);

    /* READ CACHE DIR to check for remotely deleted files */
    dirp = opendir(srch_c);
    if (dirp) {
        dbufsize = dirent_buf_size(dirp);
        dbuf = malloc(dbufsize);
    }
    else
        dbuf = NULL;

    if (!dbuf) {
        free(srch_r);
        free(srch);
        free(srch_c);
        return;
    }

    while (ONLINE && dirp && (res = readdir_r(dirp, dbuf, &ent)) == 0 && ent) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if (!bst_contains(found_tree, djb2(ent->d_name, SIZE_MAX))) {
            p = join_path2(srch, srch_len, ent->d_name, 0);
            if (!p)
                break;

            if (!has_lock(p, LOCK_OPEN) && !has_job(p, JOB_PUSH)) {
                VERBOSE("removing missing file %s/%s from cache\n", (strcmp(srch, "/")) ? srch : "", ent->d_name);
                delete_or_backup(p, CONFLICT_KEEP_REMOTE);
            }
            free(p);
        }
    }

    bst_free(found_tree, NULL);
    free(srch_c);
    free(srch_r);
    if (strcmp(srch, "/") != 0)
        free(srch);
    if (dirp)
        closedir(dirp);
    free(dbuf);
}


/* ====== WORKER THREADS ====== */
void *worker_statecheck(void *arg)
{
    int oldstate = STATE_OFFLINE;

    while (oldstate != STATE_EXITING) {
        sleep(SLEEP_SHORT);

        /* check and set state */
        if (is_running(fs2go_options.pid_file)
                && is_reachable(fs2go_options.host)
                && is_mounted(fs2go_options.remote_root)) {

            set_state(STATE_ONLINE, &oldstate);

            if (oldstate == STATE_OFFLINE)
                worker_wakeup();
        }
        else {
            set_state(STATE_OFFLINE, &oldstate);
        }
    }

    return NULL;
}

void *worker_main(void *arg)
{
#define ABORT_CURRENT() { current = 0; \
    remove_lock(j_current.path, LOCK_TRANSFER); \
    free_job(&j_current); \
    transfer_abort(); }
    int state, transfer_result;

    queue *jobs = q_init();
    queue *search = q_init();

    struct job *j;

    long long current = 0;
    struct job j_current;
    JOB_INIT(&j_current);

    char *pread=NULL, *pwrite=NULL;
    size_t p_len;

    /* "main" loop.
       - if online:
       - job in db and file not locked: perform job
       - else scan remote fs for changed files
       - sleep
     */
    while ((state = get_state()) != STATE_EXITING) {

        /* flush job scheduling queue to db */
        job_store_queue();

        /* flush sync change queue to db */
        sync_store();

        if (state == STATE_ONLINE) {
            if (worker_has_block()) {
                worker_sleep(SLEEP_LONG);
                continue;
            }

            /* if a transfer job is in progress, try to resume it */
            if (current) {
                transfer_result = transfer(NULL, NULL);

                switch (transfer_result) {
                    case TRANSFER_FAIL:
                        ABORT_CURRENT();
                        break;
                    case TRANSFER_FINISH:
                        remove_lock(j_current.path, LOCK_TRANSFER);
                        sync_set(j_current.path);
                        db_delete_job_id(current);
                        current = 0;
                        j_current.rowid = 0;
                        free_job(&j_current);
                        break;
                }

                continue;
            }

            /* no current job, get a new one */
            db_get_jobs(jobs);

            /* no jobs in db */
            if (q_empty(jobs)) {
                /* continue looking for jobs in top item on "search" queue,
                   if the queue is empty, sleep a while
                   (scan_remote will start in remote fs's root if the queue is empty)
                 */
                if (q_empty(search))
                    worker_sleep(fs2go_options.scan_interval);

                scan_remote(search);

                continue;
            }

            /* get job from queue */
            j = q_dequeue(jobs);

            /* skip locked files */
            while (j && (j->op & (JOB_PUSH|JOB_PULL)) && has_lock(j->path, LOCK_OPEN)) {
                DEBUG("%s is locked, NEXT\n", j->path);
                free_job2(j);
                j = q_dequeue(jobs);
            }
            if (!j) {
                worker_sleep(SLEEP_LONG);
                continue;
            }

            p_len = strlen(j->path);
            switch (j->op) {
                case JOB_PUSHATTR:
                    pread = cache_path2(j->path, p_len);
                    pwrite = remote_path2(j->path, p_len);
                case JOB_PULLATTR:
                    if (j->op != JOB_PUSHATTR) {
                        pread = remote_path2(j->path, p_len);
                        pwrite = cache_path2(j->path, p_len);
                    }
                    copy_attrs(pread, pwrite);
                    sync_set(j->path);
                    db_delete_job_id(j->rowid);
                    free(pread);
                    free(pwrite);
                    break;

                case JOB_PUSH:
                    if (sync_get(j->path) & (SYNC_MOD|SYNC_NEW)) {
                        DEBUG("conflict\n");
                        conflict_handle(j, NULL);
                        db_delete_job_id(j->rowid);
                        break;
                    }
                case JOB_PULL:
                    transfer_result = transfer_begin(j);

                    if (transfer_result == TRANSFER_OK) {
                        current = j->rowid;
                        JOB_INIT(&j_current);
                        j_current.rowid = j->rowid;
                        j_current.op = j->op;
                        j_current.path = strdup(j->path);
                    }
                    else {
                        if (transfer_result == TRANSFER_FINISH) {
                            sync_set(j->path);
                            db_delete_job_id(j->rowid);
                        }
                        else {
                            ERROR("transfering '%s' failed\n", j->path);
                            if (j->attempts < JOB_MAX_ATTEMPTS)
                                db_defer_job(j->rowid);
                            else {
                                VERBOSE("number of retries exhausted, giving up\n");
                                db_delete_job_id(j->rowid);
                            }
                        }

                        current = 0;
                        if (j_current.rowid)
                            free_job(&j_current);
                    }
                    break;

                default:
                    if (do_job_remote(j) != 0) {
                        log_error("performing job in worker_main");
                        VERBOSE("path: %s\n", j->path);
                    }
                    db_delete_job_id(j->rowid);
            }

            free_job2(j);
            q_clear(jobs, free_job2);
        }
        /* OFFLINE */
        else {
            worker_sleep(SLEEP_LONG);
        }
    }

    VERBOSE("exiting job thread\n");
    if (current) {
        ABORT_CURRENT();
    }
    return NULL;
#undef ABORT_CURRENT
}
