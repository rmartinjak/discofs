/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */


#include "config.h"

#include "fsops.h"
#include "log.h"

#include <fuse.h>
#include <sys/types.h>

static unsigned long debug_op_id = 0;

static void breakpoint(void)
{
    ;
}

void *debug_op_init(struct fuse_conn_info *conn)
{
    return op_init(conn);
}

void debug_op_destroy(void *p)
{
    op_destroy(p);
}

int debug_op_getattr(const char *path, struct stat *buf)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] getattr(%s)\n", id, path);
    breakpoint();
    res = op_getattr(path, buf);
    FSOP("[%d] getattr(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_fgetattr(const char *path, struct stat *buf, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] fgetattr(%s, %d)\n", id, path, FI_FD(fi));
    breakpoint();
    res = op_fgetattr(path, buf, fi);
    FSOP("[%d] fgetattr(%s, %d) returns %d\n", id, path, FI_FD(fi), res);
    return res;
}

int debug_op_access(const char *path, int mode)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] access(%s, %d)\n", id, path, mode);
    breakpoint();
    res = op_access(path, mode);
    FSOP("[%d] access(%s, %d) returns %d\n", id, path, mode, res);
    return res;
}

int debug_op_readlink(const char *path, char *buf, size_t bufsize)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] readlink(%s)\n", id, path);
    breakpoint();
    res = op_readlink(path, buf, bufsize);
    FSOP("[%d] readlink(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_opendir(const char *path, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] opendir(%s)\n", id, path);
    breakpoint();
    res = op_opendir(path, fi);
    FSOP("[%d] opendir(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] readdir(%s)\n", id, path);
    breakpoint();
    res = op_readdir(path, buf, filler, offset, fi);
    FSOP("[%d] readdir(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_releasedir(const char* path, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] releasedir(%s)\n", id, path);
    breakpoint();
    res = op_releasedir(path, fi);
    FSOP("[%d] releasedir(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] mknod(%s, %d, %d)\n", id, path, mode, rdev);
    breakpoint();
    res = op_mknod(path, mode, rdev);
    FSOP("[%d] mknod(%s, %d, %d) returns %d\n", id, path, mode, rdev, res);
    return res;
}

int debug_op_mkdir(const char *path, mode_t mode)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] mkdir(%s, %d)\n", id, path, mode);
    breakpoint();
    res = op_mkdir(path, mode);
    FSOP("[%d] mkdir(%s, %d) returns %d\n", id, path, mode, res);
    return res;
}

int debug_op_rmdir(const char *path)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] rmdir(%s)\n", id, path);
    breakpoint();
    res = op_rmdir(path);
    FSOP("[%d] rmdir(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_unlink(const char *path)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] unlink(%s)\n", id, path);
    breakpoint();
    res = op_unlink(path);
    FSOP("[%d] unlink(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_link(const char *from, const char *to)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] link(%s, %s)\n", id, from, to);
    breakpoint();
    res = op_link(from, to);
    FSOP("[%d] link(%s, %s) returns %d\n", id, from, to, res);
    return res;
}

int debug_op_symlink(const char *to, const char *from)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] symlink(%s, %s)\n", id, to, from);
    breakpoint();
    res = op_symlink(to, from);
    FSOP("[%d] symlink(%s, %s) returns %d\n", id, to, from, res);
    return res;
}

int debug_op_rename(const char *from, const char *to)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] rename(%s, %s)\n", id, from, to);
    breakpoint();
    res = op_rename(from, to);
    FSOP("[%d] rename(%s, %s) returns %d\n", id, from, to, res);
    return res;
}

int debug_op_open(const char *path, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] open(%s, %o)\n", id, path, fi->flags);
    breakpoint();
    res = op_open(path, fi);
    FSOP("[%d] open(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] create(%s, %o, %o)\n", id, path, mode, fi->flags);
    breakpoint();
    res = op_create(path, mode, fi);
    FSOP("[%d] create(%s, %o) returns %d\n", id, path, mode, res);
    return res;
}

int debug_op_flush(const char *path, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] flush(%s, %d)\n", id, path, FI_FD(fi));
    breakpoint();
    res = op_flush(path, fi);
    FSOP("[%d] flush(%s, %d) returns %d\n", id, path, FI_FD(fi), res);
    return res;
}

int debug_op_release(const char *path, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] release(%s, %d)\n", id, path, FI_FD(fi));
    breakpoint();
    res = op_release(path, fi);
    FSOP("[%d] release(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] fsync(%s, %d, %d)\n", id, path, isdatasync, FI_FD(fi));
    breakpoint();
    res = op_fsync(path, isdatasync, fi);
    FSOP("[%d] fsync(%s, %d, %d) returns %d\n", id, path, isdatasync, FI_FD(fi), res);
    return res;
}

int debug_op_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] fsyncdir(%s, %d)\n", id, path, isdatasync);
    breakpoint();
    res = op_fsyncdir(path, isdatasync, fi);
    FSOP("[%d] fsyncdir(%s, %d) returns %d\n", id, path, isdatasync, res);
    return res;
}

int debug_op_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] read(%s, %d, %lld, %d)\n", id, path, size, offset, FI_FD(fi));
    breakpoint();
    res = op_read(path, buf, size, offset, fi);
    FSOP("[%d] read(%s, %d, %lld, %d) returns %d\n", id, path, size, offset, FI_FD(fi), res);
    return res;
}

int debug_op_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] write(%s, %u, %lld, %d)\n", id, path, size, offset, FI_FD(fi));
    breakpoint();
    res = op_write(path, buf, size, offset, fi);
    FSOP("[%d] write(%s, %u, %lld, %d) returns %d\n", id, path, size, offset, FI_FD(fi), res);
    return res;
}

int debug_op_truncate(const char *path, off_t size)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] truncate(%s, %lld)\n", id, path, size);
    breakpoint();
    res = op_truncate(path, size);
    FSOP("[%d] truncate(%s, %lld) returns %d\n", id, path, size, res);
    return res;
}

int debug_op_chown(const char *path, uid_t uid, gid_t gid)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] chown(%s, %d, %d)\n", id, path, uid, gid);
    breakpoint();
    res = op_chown(path, uid, gid);
    FSOP("[%d] chown(%s, %d, %d) returns %d\n", id, path, uid, gid, res);
    return res;
}

int debug_op_chmod(const char *path, mode_t mode)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] chmod(%s, %d)\n", id, path, mode);
    breakpoint();
    res = op_chmod(path, mode);
    FSOP("[%d] chmod(%s, %d) returns %d\n", id, path, mode, res);
    return res;
}

int debug_op_utimens(const char *path, const struct timespec ts[2])
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] utimens(%s)\n", id, path);
    breakpoint();
    res = op_utimens(path, ts);
    FSOP("[%d] utimens(%s) returns %d\n", id, path, res);
    return res;
}

int debug_op_statfs(const char *path, struct statvfs *buf)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] statfs(%s)\n", id, path);
    breakpoint();
    res = op_statfs(path, buf);
    FSOP("[%d] statfs(%s) returns %d\n", id, path, res);
    return res;
}

#if HAVE_SETXATTR
int debug_op_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] setxattr(%s, %s, %s, %d, %d)\n", id, path, name, value, size, flags);
    breakpoint();
    res = op_setxattr(path, name, value, size, flags);
    FSOP("[%d] setxattr(%s, %s, %s, %d, %d) returns %d\n", id, path, name, value, size, flags, res);
    return res;
}

int debug_op_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] getxattr(%s, %s, %d)\n", id, path, name, size);
    breakpoint();
    res = op_getxattr(path, name, value, size);
    FSOP("[%d] getxattr(%s, %s, %d) returns %d\n", id, path, name, size, res);
    return res;
}

int debug_op_listxattr(const char *path, char *list, size_t size)
{
    int id, res;
    id = debug_op_id++;
    FSOP("[%d] listxattr(%s, %d)\n", id, path, size);
    breakpoint();
    res = op_listxattr(path, list, size);
    FSOP("[%d] listxattr(%s, %d) returns %d\n", id, path, size, res);
    return res;
}
#endif

