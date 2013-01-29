/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_DB_H
#define DISCOFS_DB_H

#include "config.h"
#include "discofs.h"
#include "queue.h"
#include "job.h"
#include "sync.h"

#include <sqlite3.h>
#include <sys/types.h>

/*=============*
 * DEFINITIONS *
 *=============*/

/* config option names */
#define CFG_VERSION "version"
#define CFG_FS_FEATURES "fs_features"

/*--------------*
 * return codes *
 *--------------*/

#define DB_OK 0
#define DB_ERROR -1
#define DB_NOTFOUND 404


/*====================*
 * EXPORTED FUNCTIONS *
 *====================*/

/* initialize sqlite db in file _fn_.
   if _clear_ is non-zero, remove ALL the data */
int db_init(const char *fn, int clear);

/* free all resources */
int db_destroy(void);


/*--------*
 * config *
 *--------*/

/* set/retrieve int */
int db_cfg_set_int(const char *option, int val);
int db_cfg_get_int(const char *option, int *buf);

/* set/retrieve string */
int db_cfg_set_str(char *option, const char *val);
int db_cfg_get_str(const char *option, char **buf);

/* delete int/string */
int db_cfg_delete(const char *option);


/*------*
 * jobs *
 *------*/

/* store a job in the database */
int db_job_store(const struct job *j);

/* get the next highest-priority job */
int db_job_get(struct job **j);

/* return non-zero if a job matching _path_ and _opmask_ exits in the db */
int db_job_exists(const char *path, int opmask);

/* delete job with _id_ */
int db_job_delete_id(job_id id);

/* delete all jobs for path _path_ with a job mathching _opmask_ */
int db_job_delete(const char *path, int opmask);

/* delete all RENAME jobs, where the new name equals _path_ */
int db_job_delete_rename_to(const char *path);

/* rename job entries */
int db_job_rename_file(const char *from, const char *to);
int db_job_rename_dir(const char *from, const char *to);


/*------*
 * sync *
 *------*/

/* load all sync entries from the db and pass them to _callback_ */
int db_load_sync(sync_load_cb_t callback);

/* store one sync entry */
int db_store_sync(const struct sync *s);

/* delete sync entry maching _path_ */
int db_sync_delete_path(const char *path);

/* rename sync entries */
int db_sync_rename_file(const char *from, const char *to);
int db_sync_rename_dir(const char *from, const char *to);


/*-----------*
 * hardlinks *
 *-----------*/

/* stores all hardlinks that poit to _inode_ in _q_ */
int db_hardlink_get(ino_t inode, queue *q);

/* add / remove hardlinks */
int db_hardlink_add(const char *path, ino_t inode);
int db_hardlink_remove(const char *path);

/* rename hardlink entries */
int db_hardlink_rename_file(const char *from, const char *to);
int db_hardlink_rename_dir(const char *from, const char *to);

#endif
