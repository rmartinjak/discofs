/*! @file conflict.h
 * functions for handling conflict.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_CONFLICT_H
#define DISCOFS_CONFLICT_H

#include "discofs.h"
#include "job.h"

#define CONFLICT_KEEP_CACHE 0
#define CONFLICT_KEEP_REMOTE 1
#define CONFLICT_DELETED 0
#define CONFLICT_BACKEDUP 1

/*! handle a conflict that occured when trying to perform a job.
   store either CONFLICT_KEEP_CACHE or CONFLICT_KEEP_REMOTE in
   *keep_which, so the caller knows which file was kept */
int conflict_handle(const char *path, job_op op, int *keep_which);

/*! adds discofs_options.bprefix and .suffix to file name.
  @return NULL if neither bprefix or suffix is set */
char *conflict_path(const char *path);

/*! delete file or back up if discofs_options.bprefix or .bsuffix set. 
   if file is backed up, automatically schedules a job to synchronise it
   @return CONFLICT_DELETED if file was deleted
   @return CONFLICT_BACKEDUP if backed up
 */
int delete_or_backup(const char *path, int keep);

#endif
