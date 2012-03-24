/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_JOB_H
#define FS2GO_JOB_H

#include "config.h"
#include "fs2go.h"

#define PRIO_LOW    0
#define PRIO_MID    1
#define PRIO_HIGH   2

#define PRIO_LOW_JOBS   (JOB_PUSH | JOB_PULL)
#define PRIO_HIGH_JOBS  (JOB_UNLINK | JOB_CREATE)

#define OP_PRIO_LOW(op)  ((op) & PRIO_LOW_JOBS)
#define OP_PRIO_HIGH(op) ((op) & PRIO_HIGH_JOBS)
#define OP_PRIO_MID(op)  (!(OP_PRIO_HIGH(op) || OP_PRIO_LOW(op)))

#define OP_PRIO(op) ((OP_PRIO_LOW(op)) ? PRIO_LOW : ((OP_PRIO_HIGH(op)) ? PRIO_HIGH : PRIO_MID))

#define JOB_ANY         ((unsigned int)-1)
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
#define JOB_CREATE      (1U << 11)

/* "reason" values for job_return() */
#define JOB_DONE    0
#define JOB_FAILED  1
#define JOB_LOCKED  2

#define JOB_MAX_ATTEMPTS    5
#define JOB_DEFER_TIME      10

typedef long job_id;
typedef unsigned int job_op;
typedef long job_param;

struct job
{
    job_id id;
    job_op op;
    char *path;
    time_t time;
    unsigned int attempts;
    job_param n1;
    job_param n2;
    char *s1;
    char *s2;
};

int job_init(void);
void job_destroy(void);
int job_store(void);

struct job *job_alloc(void);
void job_free(void *p);
char *job_opstr(job_op mask);

int job_schedule(job_op op, const char *path, job_param n1, job_param n2, const char *s1, const char *s2);
#define job_schedule_push(path) job_schedule(JOB_PUSH, path, 0, 0, NULL, NULL)
#define job_schedule_pull(path) job_schedule(JOB_PULL, path, 0, 0, NULL, NULL)


struct job *job_get(void);
void job_return(struct job *j, int reason);

int job_exists(const char *path, job_op mask);

int job_rename_dir(const char *from, const char *to);
int job_rename_file(const char *from, const char *to);

int job_delete(const char *path, job_op mask);
int job_delete_rename_to(const char *path);

#endif
