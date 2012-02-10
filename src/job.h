#ifndef FS2GO_JOB_H
#define FS2GO_JOB_H

#include "config.h"
#include "fs2go.h"

#include <sys/stat.h>

extern struct options fs2go_options;
extern char *croot;
extern char *rroot;

#define PRIO_LOW 1
#define PRIO_MID 2
#define PRIO_HIGH 3

#define JOB_ANY -1
#define JOB_PULL	(1 << 0)
#define JOB_PULLATTR	(1 << 1)
#define JOB_PUSH	(1 << 2)
#define JOB_PUSHATTR	(1 << 3)
#define JOB_RENAME	(1 << 4)
#define JOB_UNLINK	(1 << 5)
#define JOB_SYMLINK	(1 << 6)
#define JOB_MKDIR	(1 << 7)
#define JOB_RMDIR	(1 << 8)
#define JOB_CHMOD	(1 << 9)
#define JOB_CHOWN	(1 << 10)
#define JOB_SETXATTR	(1 << 11)

#define JOB_MAX_ATTEMPTS 5
#define JOB_DEFER_TIME 10

typedef long long rowid_t;
typedef long long jobp_t;

struct job {
	rowid_t rowid;
	int prio;
	int op;
	int attempts;
	char *path;
	jobp_t param1;
	jobp_t param2;
	char *sparam1;
	char *sparam2;
};

#define JOB_INIT(p) { memset(p, 0, sizeof(struct job)); (p)->path = NULL; (p)->sparam1 = NULL; (p)->sparam2 = NULL; }
void free_job(void *p);
void free_job2(void *p);

int job_store_queue(void);

int job(int op, const char *path, jobp_t p1, jobp_t p2, const char *sp1, const char *sp2);

int has_job(const char *path, int opmask);

int delete_jobs(const char *path, int opmask);

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
