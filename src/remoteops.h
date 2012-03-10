/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_REMOTEOPS_H
#define FS2GO_REMOTEOPS_H

int remoteop_unlink(const char *path);

int remoteop_rename(const char *from, const char *to);

int remoteop_chown(const char *path, uid_t uid, gid_t gid);

int remoteop_chmod(const char *path, mode_t mode);

#if HAVE_SETXATTR
int remoteop_setxattr(const char *path, const char *name, const char *value,
    size_t size, int flags);
#endif

#endif
