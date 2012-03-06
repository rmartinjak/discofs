/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_FSOPS_H
#define FS2GO_FSOPS_H

#include "config.h"
#include "fs2go.h"
#include <fuse.h>
#include <pthread.h>
#include <sys/types.h>

#define FH_SIZE (sizeof(int) * 2)
#define FH_FD(fh) ((int*)fh)[0]
#define FH_FLAGS(fh) ((int*)fh)[1]

#define FI_FD(fi) FH_FD((fi->fh))
#define FI_FLAGS(fi) FH_FLAGS((fi->fh))

#define FH_WRITTEN 1

void *op_init(struct fuse_conn_info *conn);
void op_destroy(void *p);

int op_getattr(const char *path, struct stat *buf);
int op_fgetattr(const char *path, struct stat *buf, struct fuse_file_info *fi);
int op_access(const char *path, int mode);
int op_readlink(const char *path, char *buf, size_t bufsize);
int op_opendir(const char *path, struct fuse_file_info *fi);
int op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int op_mknod(const char *path, mode_t mode, dev_t rdev);
int op_mkdir(const char *path, mode_t mode);
int op_rmdir(const char *path);
int op_unlink(const char *path);
int op_link(const char *from, const char *to);
int op_symlink(const char *to, const char *from);
int op_rename(const char *from, const char *to);
int op_releasedir(const char* path, struct fuse_file_info *fi);
int op_open(const char *path, struct fuse_file_info *fi);
int op_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int op_flush(const char *path, struct fuse_file_info *fi);
int op_release(const char *path, struct fuse_file_info *fi);
int op_fsync(const char *path, int isdatasync, struct fuse_file_info *fi);
int op_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi);
int op_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int op_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int op_truncate(const char *path, off_t size);
int op_chown(const char *path, uid_t uid, gid_t gid);
int op_chmod(const char *path, mode_t mode);
int op_utimens(const char *path, const struct timespec ts[2]);
int op_statfs(const char *path, struct statvfs *buf);

#ifdef HAVE_SETXATTR
int op_setxattr(const char *path, const char *name, const char *value, size_t size, int flags);
int op_getxattr(const char *path, const char *name, char *value, size_t size);
int op_listxattr(const char *path, char *list, size_t size);
#endif

#endif
