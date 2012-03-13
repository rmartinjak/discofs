/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

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
#define SCHEMA_JOB \
    "rowid INTEGER PRIMARY KEY," \
    "prio INTEGER," \
    "op INTEGER," \
    "time INTEGER," \
    "attempts INTEGER," \
    "path TEXT," \
    "n1 INTEGER," \
    "n2 INTEGER," \
    "s1 TEXT," \
    "s2 TEXT"

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


int db_job_store(const struct job *j);
int db_job_get(struct job **j);
int db_job_exists(const char *path, int opmask);

int db_job_delete_id(job_id id);
int db_job_delete(const char* path, int opmask);

int db_job_delete_rename_to(const char *path);

int db_get_job_by_id(struct job *j, long long id);


int db_load_sync(sync_load_cb_t callback);
int db_store_sync(const struct sync *s);

int db_rename_file(const char *from, const char *to);
int db_rename_dir(const char *from, const char *to);
int db_delete_path(const char *path);


int db_job_delete_path(const char *path);
int db_job_rename_file(const char *from, const char *to);
int db_job_rename_dir(const char *from, const char *to);

int db_sync_delete_path(const char *path);
int db_sync_rename_file(const char *from, const char *to);
int db_sync_rename_dir(const char *from, const char *to);

#endif
