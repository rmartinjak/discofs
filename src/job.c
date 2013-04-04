/*! @file job.c
 * job handling.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "job.h"

#include "discofs.h"
#include "log.h"
#include "queue.h"
#include "db.h"

#include <pthread.h>

/*=============*
 * DEFINITIONS *
 *=============*/

#define JOB_STR_BUF_N   5
#define JOB_STR_BUF_SZ  1024

/*! job queue */
static queue *job_q = NULL;
static pthread_mutex_t m_job_q = PTHREAD_MUTEX_INITIALIZER;


/*-------------------*
 * static prototypes *
 *-------------------*/

static int job_q_enqueue(struct job *j);

/*==================*
 * STATIC FUNCTIONS *
 *==================*/

static int job_q_enqueue(struct job *j)
{
    int res;

    pthread_mutex_lock(&m_job_q);

    res = q_enqueue(job_q, j);

    pthread_mutex_unlock(&m_job_q);

    return res;
}


/*====================*
 * EXPORTED FUNCTIONS *
 *====================*/

int job_init(void)
{
    job_q = q_init();
    if (!job_q)
        return -1;

    return 0;
}

void job_destroy(void)
{
    job_store();

    q_free(job_q, NULL);
}

/*! store jobs in db */
int job_store(void)
{
    int res = DB_OK;
    struct job *j;

    /* nothing to do */
    if (q_empty(job_q))
        return 0;

    pthread_mutex_lock(&m_job_q);

    /* store all jobs from job queue */
    while (res == DB_OK && (j = q_dequeue(job_q)))
    {
        /* only one PUSH or PULL job should exist */
        if (j->op == JOB_PUSH || j->op == JOB_PULL)
            db_job_delete(j->path, JOB_PUSH|JOB_PULL);

        res = db_job_store(j);

        /* free the job object if storing in db was OK */
        if (res == DB_OK)
            job_free(j);
    }

    pthread_mutex_unlock(&m_job_q);

    if (res != DB_OK)
        return -1;
    return 0;
}


struct job *job_alloc(void)
{
    struct job *j = malloc(sizeof *j);

    if (j)
    {
        j->path = NULL;
        j->s1 = NULL;
        j->s2 = NULL;
    }

    return j;
}

void job_free(void *p)
{
    struct job *j = p;

    if (!j)
        return;

    free(j->path);
    free(j->s1);
    free(j->s2);
    free(j);
}

char *job_opstr(job_op mask)
{
    static char buf[JOB_STR_BUF_N][JOB_STR_BUF_SZ];
    static int  n = 0;
    n = (n + 1) % JOB_STR_BUF_N;

    buf[n][0] = '\0';

    if (mask == JOB_ANY)
        strcat(buf[n], "JOB_ANY|");
    else
    {
        #define OPSTR(op) if (mask & op) strcat(buf[n], #op "|")
        OPSTR(JOB_PULL);
        OPSTR(JOB_PUSH);
        OPSTR(JOB_RENAME);
        OPSTR(JOB_UNLINK);
        OPSTR(JOB_SYMLINK);
        OPSTR(JOB_LINK);
        OPSTR(JOB_MKDIR);
        OPSTR(JOB_RMDIR);
        OPSTR(JOB_CHMOD);
        OPSTR(JOB_CHOWN);
        OPSTR(JOB_SETXATTR);
        OPSTR(JOB_CREATE);
        #undef OPSTR
    }

    buf [n] [strlen(buf[n]) - 1] = '\0';

    return buf[n];
}


int job_schedule(job_op op, const char *path, job_param n1, job_param n2, const char *s1, const char *s2)
{
    struct job *j;

    /* don't schedule new PUSH/PULL if one already exists */
    if (op == JOB_PUSH || op == JOB_PULL)
    {
        if (job_exists(path, op))
            return 0;
    }

    j = job_alloc();
    if (!j)
        return -1;

    j->path = strdup(path);
    j->s1 = (s1) ? strdup(s1) : NULL;
    j->s2 = (s2) ? strdup(s2) : NULL;

    /* if any strdup() failed, free alloc'd job and return -1 */
    if (!j->path || (s1 && !j->s1) || (s2 && !j->s2))
    {
        job_free(j);
        return -1;
    }

    j->id = -1;
    j->op = op;
    j->time = time(NULL);
    j->attempts = 0;
    j->n1 = n1;
    j->n2 = n2;

    if (op == JOB_PUSH || op == JOB_PULL)
        j->time += JOB_DEFER_TIME;

    job_q_enqueue(j);

    return 0;
}

void job_return(struct job *j, int reason)
{
    if (!j)
        return;

    VERBOSE("job %s on %s returned: %s\n", job_opstr(j->op), j->path,
            (reason == JOB_DONE) ? "done" :
            ((reason == JOB_FAILED) ? "failed" : "file is locked")
         );

    if (reason == JOB_DONE)
    {
        if (j->op == JOB_UNLINK)
            sync_delete_file(j->path);
        else if (j->op == JOB_RMDIR)
            sync_delete_dir(j->path);
        else
            sync_set(j->path, 0);

        db_job_delete_id(j->id);
        job_free(j);
        return;
    }

    if (reason == JOB_FAILED)
    {
        j->attempts++;
        if (j->attempts > JOB_MAX_ATTEMPTS)
        {
            ERROR("number of retries exhausted, giving up\n");
            db_job_delete_id(j->id);
            job_free(j);
            return;
        }
    }

    /* JOB_LOCKED or JOB_FAILED and attempt limit not reached */

    j->time = time(NULL) + JOB_DEFER_TIME;

    job_q_enqueue(j);
}

struct job *job_get(void)
{
   struct job *j;

   job_store();

   db_job_get(&j);

   return j;
}

int job_exists(const char *path, job_op mask)
{
    job_store();

    if (db_job_exists(path, mask) != DB_OK)
        return -1;
    return 0;
}

int job_rename_dir(const char *from, const char *to)
{
    job_store();

    if (db_job_rename_dir(from, to) != DB_OK)
        return -1;
    return 0;
}

int job_rename_file(const char *from, const char *to)
{
    job_store();

    if (db_job_rename_file(from, to) != DB_OK)
        return -1;
    return 0;
}

int job_delete(const char *path, job_op mask)
{
    job_store();
    if (db_job_delete(path, mask) != DB_OK)
        return -1;

    return 0;
}

int job_delete_rename_to(const char *path)
{
    job_store();

    if (db_job_delete_rename_to(path) != DB_OK)
        return -1;
    return 0;
}
