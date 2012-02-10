#ifndef FS2GO_DB_H
#define FS2GO_DB_H

#include "config.h"
#include "fs2go.h"
#include "queue.h"
#include "job.h"
#include "sync.h"
#include "hashtable.h"

#include <sqlite3.h>

#define DB_OK 0
#define DB_ERROR -1
#define DB_NOTFOUND 404

#define TABLE_CFG "config"
#define SCHEMA_CFG "option TEXT UNIQUE, nval INTEGER, tval TEXT"

#define CFG_VERSION "version"
#define CFG_FS_FEATURES "fs_features"


#define TABLE_JOB "job"
#define SCHEMA_JOB "prio INTEGER," \
			"op INTEGER," \
			"attempts INTEGER," \
			"time_s INTEGER," \
			"time_ns INTEGER," \
			"path TEXT," \
			"param1 INTEGER," \
			"param2 INTEGER," \
			"sparam1 TEXT," \
			"sparam2 TEXT"

#define TABLE_SYNC "sync"
#define SCHEMA_SYNC "path TEXT UNIQUE NOT NULL," \
			"mtime_s INTEGER," \
			"mtime_ns INTEGER," \
			"ctime_s INTEGER," \
			"ctime_ns INTEGER"
void db_open(void);
void db_close(void);

int db_init(const char *fn, int clear);
int db_destroy(void);


int db_cfg_delete(const char *option);
int db_cfg_set_int(const char *option, int val);
int db_cfg_set_str(char *option, const char *val);
int db_cfg_get_int(const char *option, int *buf);
int db_cfg_get_str(const char *option, char **buf);


int db_store_job(const struct job *j);
int db_defer_job(rowid_t id);

int db_has_job(const char *path, int opmask);
int db_get_jobs(queue *qu);
int db_get_job_by_id(struct job *j, rowid_t id);

int db_delete_jobs(const char* path, int opmask);
int db_delete_job_id(rowid_t id);

int db_load_sync(void);

int db_get_sync(const char *path, struct sync *s);
int db_store_sync(const struct sync *s);
int db_delete_sync(const char *path);

int db_rename_file(const char *from, const char *to);
int db_rename_dir(const char *from, const char *to);
int db_delete_path(const char *path);

#endif
