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

int conflict_handle(const struct job *j, int *keep_which);
char *conflict_path(const char *path);

int delete_or_backup(const char *path, int keep);

#endif
