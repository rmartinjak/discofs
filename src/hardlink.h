/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_HARDLINK_H
#define DISCOFS_HARDLINK_H

#include <sys/types.h>

int hardlink_add(const char *path, ino_t inode);
int hardlink_create(const char *path, ino_t inode);

int hardlink_remove(const char *path);
int hardlink_sync_set(ino_t inode);

int hardlink_rename_dir(const char *from, const char *to);
int hardlink_rename_file(const char *from, const char *to);

#endif
