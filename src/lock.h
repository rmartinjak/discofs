/*! @file lock.h
 * file locking.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_LOCK_H
#define DISCOFS_LOCK_H

#include "config.h"

#define LOCK_TYPE_BITS 1
#define LOCK_OPEN 0
#define LOCK_TRANSFER 1

int lock_init(void);
void lock_destroy(void);

int lock_has(const char *path, int type);
int lock_set(const char *path, int type);
int lock_remove(const char *path, int type);

#endif
