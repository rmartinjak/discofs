/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_LOCK_H
#define FS2GO_LOCK_H

#include "config.h"

#define LOCK_TYPE_BITS 1
#define LOCK_OPEN 0
#define LOCK_TRANSFER 1

int has_lock(const char *path, int type);
int set_lock(const char *path, int type);
int remove_lock(const char *path, int type);

#endif
