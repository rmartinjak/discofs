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

static queue job_queue = QUEUE_INIT;
pthread_mutex_t m_job_queue = PTHREAD_MUTEX_INITIALIZER;


/*-------------------*/
/* static prototypes */
/*-------------------*/

static job_id job_new_id(void);


/*==================*/
/* STATIC FUNCTIONS */
/*==================*/

static job_id job_new_id(void)
{
    static job_id new_id = 1;
    static pthread_mutex_t m_new_id = PTHREAD_MUTEX_INITIALIZER;

    job_id id;

    pthread_mutex_lock(&m_new_id);
    id = new_id++;
    pthread_mutex_unlock(&m_new_id);

    return id;
}


/*====================*/
/* EXPORTED FUNCTIONS */
/*====================*/

void job_free(void *p)
{
    struct job *j = (struct job *)p;

#define FREE(p) free(p), p = NULL
    FREE(j->path);
    FREE(j->sparam1);
    FREE(j->sparam2);
#undef FREE
}

void job_free2(void *p)
{
    free_job(p);
    free(p);
}

struct job *job_create(job_op op, const char *path, job_param n1, job_param n2, const char *s1, const char *s2)
{
    struct job *j = malloc(sizeof (struct job));

    if (j) {
        j->path = strdup(path);
        j->sparam1 = (s1) ? strdup(s1) : NULL;
        j->sparam2 = (s2) ? strdup(s2) : NULL;

        /* if any strdup() failed, free alloc'd memory and return NULL */
        if (!j->path || (s1 && !j->sparam1) || (s2 && !j->sparam2)) {
            free(j->path), free(j->sparam1), free(j->sparam2);
            free(j);
            return NULL;
        }

        j->id = job_new_id();
        j->op = op;
        j->nparam1 = n1;
        j->nparam2 = n2;
    }

    return j;
}

int job_schedule(job_op op, const char *path, job_param n1, job_param n2, const char *s1, const char *s2)
{
    int prio;
    struct job *j = job_create(op, path, n1, n2, s1, s2);

    prio = OP_PRIO(op);
}

/* store jobs in db */
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
