/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "remoteops.h"

#include "fs2go.h"
#include "sync.h"
#include "hardlink.h"
#include "lock.h"
#include "conflict.h"
#include "transfer.h"
#include "funcs.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif


int remoteop_rename(const char *from, const char *to)
{
    int res;
    int sync, keep;
    char *pf, *pt;

    pf = remote_path(from);
    pt = remote_path(to);
    if (!pf || !pt)
    {
        free(pf), free(pt);
        return -EIO;
    }

    /* abort eventual transfering of "to" */
    if (lock_has(to, LOCK_TRANSFER))
    {
        transfer_abort();
    }
    /* rename transfer if "from" is being transfered */
    else if (lock_has(from, LOCK_TRANSFER))
    {

        transfer_rename(to);

        /* rename transfer lock */
        lock_remove(from, LOCK_TRANSFER);
        lock_set(to, LOCK_TRANSFER);
    }

    /* renaming a dir -> rename transfer if "inside" that dir */
    else if (is_dir(pf))
    {
        transfer_rename_dir(from, to);
    }
    /* renaming a file -> check for conflict */
    else
    {
        sync = sync_get(to);
        /* target new/modified -> conflict! */
        if (sync & (SYNC_NEW | SYNC_MOD))
        {
            conflict_handle(to, JOB_RENAME, &keep);
            if (keep == CONFLICT_KEEP_REMOTE)
            {
                char *pt_alt = conflict_path(pt);
                free(pt);
                pt = pt_alt;
            }
        }
    }

    /* do the actual renaming */
    if (pt)
    {
        res = rename(pf, pt);
        free(pf);
        free(pt);
    }
    /* pt is NULL if a conflict occured, the remote file is kept
       and no backup prefix/suffix was set. this means the file/dir
       should be removed, not renamed */
    else
    {
        /* one of those will work */
        unlink(pf);
        rmdir(pf);
        res = 0;
        free(pf);
    }

    if (res)
        return -errno;

    return 0;
}

int remoteop_create(const char *path, int flags, mode_t mode)
{
    int fd;
    char *p;

    p = remote_path(path);

    fd = open(p, flags, mode);

    free(p);

    if (fd < 0)
        return -errno;

    close(fd);
    return 0;
}

int remoteop_unlink(const char *path)
{
    int res, sync;
    char *p;

    if (lock_has(path, LOCK_TRANSFER))
    {
        transfer_abort();
        lock_remove(path, LOCK_TRANSFER);
    }

    sync = sync_get(path);

    /* this would be a conflict! don't delete but pull */
    if (sync == SYNC_MOD)
    {
        job_schedule_pull(path);
        return 0;
    }

    if (sync == SYNC_NOT_FOUND)
    {
        return 0;
    }

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = unlink(p);
    free(p);

    if (res)
        return -errno;

    hardlink_remove(path);

    return 0;
}

int remoteop_symlink(const char *to, const char *path)
{
    int res;
    char *p;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = symlink(to, p);
    free(p);

    if (res)
        return -errno;

    return 0;
}

int remoteop_link(const char *to, const char *path)
{
    int res;
    char *pp, *pt;
    struct stat st;

    pp = remote_path(path);
    pt = remote_path(to);
    if (!pp || !pt)
    {
        free(pp);
        free(pt);
        return -EIO;
    }

    res = link(pt, pp);
    lstat(pp, &st);

    free(pp);
    free(pt);

    if (res)
        return -errno;

    hardlink_add(path, st.st_ino);
    hardlink_add(to, st.st_ino);

    return 0;
}

int remoteop_mkdir(const char *path, mode_t mode)
{
    int res;
    char *p;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = mkdir(p, mode);
    free(p);

    if (res)
        return -errno;

    return 0;
}

int remoteop_rmdir(const char *path)
{
    int res;
    char *p;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = rmdir(p);
    free(p);

    if (res)
    {
        /* ignore ENOENT */
        if (errno == ENOENT) return 0;

        return -errno;
    }

    return 0;
}

int remoteop_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;
    char *p;

    if (fs2go_options.copyattr & COPYATTR_NO_OWNER)
        uid = -1;
    if (fs2go_options.copyattr & COPYATTR_NO_GROUP)
        gid = -1;

    if (uid == -1 && gid == -1)
        return 0;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = chown(p, uid, gid);
    free(p);

    if (res)
        return -errno;

    return 0;
}

int remoteop_chmod(const char *path, mode_t mode)
{
    int res;
    char *p;

    if (fs2go_options.copyattr & COPYATTR_NO_MODE)
        return 0;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = chmod(p, mode);
    free (p);

    if (res)
        return -errno;

    return 0;
}

#if HAVE_SETXATTR
int remoteop_setxattr(const char *path, const char *name, const char *value,
    size_t size, int flags)
{
    int res;
    char *p;

    if (fs2go_options.copyattr & COPYATTR_NO_XATTR)
        return 0;

    p = remote_path(path);
    if (!p)
        return -EIO;

    res = lsetxattr(p, name, value, size, flags);
    free(p);

    if (res)
        return -errno;

    return 0;
}
#endif
