/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_CONFLICT_H
#define FS2GO_CONFLICT_H

#include "fs2go.h"
#include "job.h"

#define CONFLICT_KEEP_CACHE 0
#define CONFLICT_KEEP_REMOTE 1
#define CONFLICT_DELETED 0
#define CONFLICT_BACKEDUP 1

/* handle a conflict that occured when trying to perform job j.
   store either CONFLICT_KEEP_CACHE or CONFLICT_KEEP_REMOTE in
   *keep_which, so the caller knows which file was kept */
int conflict_handle(const struct job *j, int *keep_which);

/* adds fs2go_options.bprefix and .suffix to file name, or returns NULL if
   neither of them is set */
char *conflict_path(const char *path);

/* delete file or back up if fs2go_options.bprefix or .bsuffix set. returns:
   CONFLICT_DELETED if file was deleted
   CONFLICT_BACKEDUP if backed up
   if file is backed up, automatically schedules a job to synchronise it
 */
int delete_or_backup(const char *path, int keep);

#endif
