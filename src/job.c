/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "job.h"

#include "fs2go.h"
#include "log.h"
#include "queue.h"
#include "db.h"

#include <pthread.h>

/*=============*/
/* DEFINITIONS */
/*=============*/

extern struct options fs2go_options;

/* job queue */
static queue *job_q = NULL;
static pthread_mutex_t m_job_q = PTHREAD_MUTEX_INITIALIZER;


/*-------------------*/
/* static prototypes */
/*-------------------*/

static int job_q_enqueue(struct job *j);

/*==================*/
/* STATIC FUNCTIONS */
/*==================*/

static int job_q_enqueue(struct job *j)
{
    int res;

    pthread_mutex_lock(&m_job_q);

    res = q_enqueue(job_q, j);

    pthread_mutex_unlock(&m_job_q);

    return res;
}


/*====================*/
/* EXPORTED FUNCTIONS */
/*====================*/

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
    struct job *j = malloc(sizeof (struct job));

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
    struct job *j = (struct job *)p;

    if (!j)
        return;

    free(j->path);
    free(j->s1);
    free(j->s2);
    free(j);
}


int job_schedule(job_op op, const char *path, job_param n1, job_param n2, const char *s1, const char *s2)
{
    struct job *j = job_alloc();

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

void job_reschedule(struct job *j, int defer)
{
    if (!j)
        return;

    j->attempts++;

    if (defer)
        j->time += JOB_DEFER_TIME;

    job_q_enqueue(j);
}

struct job *job_get(void)
{
   struct job *j;

   db_job_get(&j);

   return j;
}

void job_done(struct job *j)
{
    if (!j)
        return;

    sync_set(j->path);
    db_job_delete_id(j->id);
    job_free(j);
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
