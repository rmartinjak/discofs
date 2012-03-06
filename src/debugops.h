/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_DEBUGOPS_H
#define FS2GO_DEBUGOPS_H

#include "config.h"
#include "fs2go.h"
#include <fuse.h>
#include <sys/types.h>

void *debug_op_init(struct fuse_conn_info *conn);
void debug_op_destroy(void *p);

int debug_op_getattr(const char *path, struct stat *buf);
int debug_op_fgetattr(const char *path, struct stat *buf, struct fuse_file_info *fi);
int debug_op_access(const char *path, int mode);
int debug_op_readlink(const char *path, char *buf, size_t bufsize);
int debug_op_opendir(const char *path, struct fuse_file_info *fi);
int debug_op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int debug_op_mknod(const char *path, mode_t mode, dev_t rdev);
int debug_op_mkdir(const char *path, mode_t mode);
int debug_op_rmdir(const char *path);
int debug_op_unlink(const char *path);
int debug_op_link(const char *from, const char *to);
int debug_op_symlink(const char *to, const char *from);
int debug_op_rename(const char *from, const char *to);
int debug_op_releasedir(const char* path, struct fuse_file_info *fi);
int debug_op_open(const char *path, struct fuse_file_info *fi);
int debug_op_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int debug_op_flush(const char *path, struct fuse_file_info *fi);
int debug_op_release(const char *path, struct fuse_file_info *fi);
int debug_op_fsync(const char *path, int isdatasync, struct fuse_file_info *fi);
int debug_op_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi);
int debug_op_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int debug_op_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int debug_op_truncate(const char *path, off_t size);
int debug_op_chown(const char *path, uid_t uid, gid_t gid);
int debug_op_chmod(const char *path, mode_t mode);
int debug_op_utimens(const char *path, const struct timespec ts[2]);
int debug_op_statfs(const char *path, struct statvfs *buf);

#ifdef HAVE_SETXATTR
int debug_op_setxattr(const char *path, const char *name, const char *value, size_t size, int flags);
int debug_op_getxattr(const char *path, const char *name, char *value, size_t size);
int debug_op_listxattr(const char *path, char *list, size_t size);
#endif

#endif
