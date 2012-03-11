/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_JOB_H
#define FS2GO_JOB_H

#include "config.h"
#include "fs2go.h"

#define PRIO_LOW    1
#define PRIO_MID    2
#define PRIO_HIGH   3

#define PRIO_LOW_JOBS   (JOB_PUSH | JOB_PULL)
#define PRIO_HIGH_JOBS  (JOB_UNLINK)

#define JOB_IS_LOW(op)  ((op) & PRIO_LOW_JOBS)
#define JOB_IS_HIGH(op) ((op) & PRIO_HIGH_JOBS)
#define JOB_IS_MID(op)  (!(JOB_IS_LOW(op) || JOB_IS_HIGH(op)))

#define JOB_ANY         ((unsigned int)-1); 
#define JOB_PULL        (1U << 0)
#define JOB_PUSH        (1U << 1)
#define JOB_RENAME      (1U << 2)
#define JOB_UNLINK      (1U << 3)
#define JOB_SYMLINK     (1U << 4)
#define JOB_LINK        (1U << 5)
#define JOB_MKDIR       (1U << 6)
#define JOB_RMDIR       (1U << 7)
#define JOB_CHMOD       (1U << 8)
#define JOB_CHOWN       (1U << 9)
#define JOB_SETXATTR    (1U << 10)

#define JOB_MAX_ATTEMPTS    5
#define JOB_DEFER_TIME      10

typedef unsigned int job_op;
typedef unsigned int job_id;
typedef long job_param;

struct job
{
    job_id id;
    job_op op;
    unsigned int attempts;
    char *path;
    job_param nparam1;
    job_param nparam2;
    char *sparam1;
    char *sparam2;
};


int job_init(void);
void job_destroy(void);

struct job *job_create(const char *path, job_op op, jobparam_t par1, jobparam_t par2, const char *spar1, const char *spar2);

void job_free(void *p);
void job_free2(void *p);

int job_store(void);

int job_perform(struct job *j);
int job_exists(const char *path, int opmask);

int job_delete(const char *path, int opmask);
int job_delete_rename_to(const char *path);

int job_rename(const char *from, const char *to);


int job(int op, const char *path, jobp_t p1, jobp_t p2, const char *sp1, const char *sp2);


int schedule_job(struct job *j);
int schedule_pp(const char *path, int op);
#define schedule_push(p) schedule_pp(p, JOB_PUSH)
#define schedule_pushattr(p) schedule_pp(p, JOB_PUSHATTR)
#define schedule_pull(p) schedule_pp(p, JOB_PULL)
#define schedule_pullattr(p) schedule_pp(p, JOB_PULLATTR)

int do_job(struct job *j, int do_remote);
#define do_job_cache(j) do_job(j, 0)
#define do_job_remote(j) do_job(j, 1)

int instant_pull(const char *path);

#endif
