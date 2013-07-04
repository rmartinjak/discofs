/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "fsops.h"

#include "discofs.h"
#include "state.h"
#include "remoteops.h"
#include "log.h"
#include "funcs.h"
#include "sync.h"
#include "job.h"
#include "lock.h"
#include "worker.h"
#include "transfer.h"
#include "bst.h"

#include <fuse.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif

extern pthread_mutex_t m_instant_pull;

static pthread_t t_worker, t_state;

/* called when fs is initialized.  starts worker and state checking thread */
void *op_init(struct fuse_conn_info *conn)
{
    VERBOSE("starting state check thread");
    if (pthread_create(&t_state, NULL, state_check_main, NULL))
        FATAL("failed to create thread\n");

    VERBOSE("starting worker thread");
    if (pthread_create(&t_worker, NULL, worker_main, NULL))
        FATAL("failed to create thread\n");

    return NULL;
}

void op_destroy(void *p)
{
    state_set(STATE_EXITING, NULL);

    DEBUG("joining state check thread");
    pthread_join(t_state, NULL);

    DEBUG("joining worker thread");
    pthread_join(t_worker, NULL);
}

int op_getattr(const char *path, struct stat *buf)
{
    int res;
    int err;
    char *p;
    size_t p_len = strlen(path);

    p = cache_path2(path, p_len);

    res = lstat(p, buf);
    err = errno;
    free(p);

    if (res && errno == ENOENT && ONLINE)
    {
        p = remote_path2(path, p_len);
        res = lstat(p, buf);
        free(p);
    }

    if (res)
        return -err;
    return 0;
}

int op_fgetattr(const char *path, struct stat *buf, struct fuse_file_info *fi)
{
    int res;

    res = fstat(FI_FD(fi), buf);

    if (res == -1)
        return -errno;
    return 0;
}

int op_access(const char *path, int mode)
{
    int res;
    int err;
    char *p;
    size_t p_len = strlen(path);

    p = cache_path2(path, p_len);

    res = access(p, mode);
    err = errno;
    free(p);

    if (res && errno == ENOENT && ONLINE)
    {
        p = remote_path2(path, p_len);
        res = access(p, mode);
        free(p);
    }

    if (res)
        return -err;
    return 0;
}

int op_readlink(const char *path, char *buf, size_t bufsize)
{
    int res;
    char *p;

    p = (ONLINE) ? remote_path(path) : cache_path(path);
    res = readlink(p, buf, bufsize);
    free(p);

    if (res == -1)
        return -errno;

    /* null-terminate buf */
    if (res < bufsize)
        buf[res] = '\0';
    else
        buf[bufsize-1] = '\0';

    return 0;
}

int op_opendir(const char *path, struct fuse_file_info *fi)
{
    char *p, *p2;
    DIR **dirp;
    DIR **d;
    size_t p_len = strlen(path);

    dirp = malloc(2 * sizeof *dirp);
    if (!dirp)
        return -EIO;

    worker_block();
    d = dirp;

    /* open cache dir */
    p = cache_path2(path, p_len);
    if ((*d = opendir(p)) == NULL)
    {
        if (errno == ENOENT && ONLINE)
        {
            p2 = remote_path2(path, p_len);
            clone_dir(p2, p);
            free(p2);
            *d = opendir(p);
        }
        else
        {
            free(p);
            free(dirp);
            return -errno;
        }
    }
    free(p);

    d++;
    if (ONLINE)
    {
        p = remote_path2(path, p_len);
        *d = opendir(p);
        free(p);
    }
    else
    {
        *d = NULL;
    }

    fi->fh = (uint64_t)dirp;
    return 0;
}

int op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    int res;
    DIR **dirp;
    bst *tree = bst_init();

    size_t dbufsize;
    struct dirent *ent;
    long hash;
    struct dirent *dbuf;
    struct stat st;
    memset(&st, 0, sizeof st);


    dirp = (DIR **)fi->fh;
    if (!*dirp)
        return -EBADF;

    dbufsize = dirent_buf_size(*dirp);
    dbuf = malloc(dbufsize);

    res = readdir_r(*dirp, dbuf, &ent);
    if (res > 0)
    {
        free(dbuf);
        return -res;
    }

    int n = 2;
    while (n-- && *dirp)
    {
        do {
            if (!ent)
                continue;

            hash = djb2(ent->d_name, SIZE_MAX);

            if (bst_contains(tree, hash))
                continue;

            memset(&st, 0, sizeof st);
            st.st_ino = ent->d_ino;
            st.st_mode = DTTOIF(ent->d_type);

            if (filler(buf, ent->d_name, &st, 0))
            {
                bst_free(tree, NULL);
                free(dbuf);
                return -ENOMEM;
            }
            bst_insert(tree, hash, NULL);
        }
        while ((res = readdir_r(*dirp, dbuf, &ent)) == 0 && ent);

        dirp++;
    }

    bst_free(tree, NULL);
    free(dbuf);

    return -res;
}

int op_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;
    size_t p_len = strlen(path);

    char *p;

    p = cache_path2(path, p_len);
    res = mknod(p, mode, rdev);
    if (res != 0)
        return -errno;
    free(p);

    if (ONLINE)
    {
        p = remote_path2(path, p_len);
        res = mknod(p, mode, rdev);
        free(p);
        if (res != 0)
            return -errno;
        sync_set(path, 0);
    }
    else
    {
        job_schedule_push(path);
    }

    return res;
}

int op_mkdir(const char *path, mode_t mode)
{
    int res;
    char *p;

    p = cache_path(path);

    if (!p)
        return -EIO;

    res = mkdir(p, mode);
    free(p);

    if (res)
        return -errno;

    sync_delete_dir(path);

    if (ONLINE)
    {
        res = remoteop_mkdir(path, mode);
        if (!res)
        {
            sync_set(path, 0);
            return 0;
        }
    }

    job_schedule(JOB_MKDIR, path, mode, 0, NULL, NULL);

    return 0;
}

int op_rmdir(const char *path)
{
    int res;
    char *p;

    p = cache_path(path);

    if (!p)
        return -EIO;

    res = rmdir(p);
    free(p);

    if (res)
        return -errno;

    sync_delete_dir(path);

    if (ONLINE)
    {
        res = remoteop_rmdir(path);
        if (!res)
            return 0;
    }

    job_schedule(JOB_RMDIR, path, 0, 0, NULL, NULL);

    return 0;
}

int op_unlink(const char *path)
{
    int res;
    char *p;

    p = cache_path(path);

    if (!p)
        return -EIO;

    res = unlink(p);
    free(p);

    if (res)
    {
        if (errno != ENOENT)
            return -errno;
    }

    job_delete(path, JOB_ANY);
    job_delete_rename_to(path);

    sync_delete_file(path);

    if (ONLINE)
    {
        res = remoteop_unlink(path);
        if (!res)
            return 0;
    }

    job_schedule(JOB_UNLINK, path, 0, 0, NULL, NULL);

    return 0;
}

int op_symlink(const char *to, const char *path)
{
    int res;
    char *p;

    p = cache_path(path);

    res = symlink(to, p);
    free(p);

    if (res)
        return -errno;

    if (ONLINE)
    {
        res = remoteop_symlink(to, path);
        if (!res)
        {
            sync_set(path, 0);
            return 0;
        }
    }

    job_schedule(JOB_SYMLINK, path, 0, 0, to, NULL);

    return 0;
}

int op_link(const char *to, const char *path)
{
    return -ENOTSUP;
}

int op_rename(const char *from, const char *to)
{
    int res = 0;
    char *pf, *pt;
    size_t len_from, len_to;
    int from_is_dir;

    len_from = strlen(from);
    len_to = strlen(to);

    pf = cache_path2(from, len_from);
    pt = cache_path2(to, len_to);
    if (!pf || !pt)
    {
        free(pf), free(pt);
        return -EIO;
    }

    /* needed later */
    from_is_dir = is_dir(pf);

    /* rename in cache */
    res = rename(pf, pt);

    free(pf);
    free(pt);

    /* error occured, return it */
    if (res)
    {
        return -errno;
    }

    /*--------------------*/
    /* delete/rename jobs */
    /*--------------------*/

    job_delete(to, JOB_ANY);

    if (from_is_dir)
    {
        job_rename_dir(from, to);
    }
    else
    {
        job_rename_file(from, to);
    }


    /*------*/
    /* lock */
    /*------*/

    /* "rename" all OPEN locks */
    while (lock_has(from, LOCK_OPEN))
    {
        lock_remove(from, LOCK_OPEN);
        lock_set(to, LOCK_OPEN);
    }


    /*------------------*/
    /* rename on remote */
    /*------------------*/

    if (ONLINE)
    {
        /* moving a file or directory may render the data collected by the
           worker thread during worker_scan_dir outdated. worker_cancel_scan()
           forces it to re-scan from the root
        */
        worker_block();
        worker_cancel_scan();
        res = remoteop_rename(from, to);
        worker_unblock();

        if (!res || errno == ENOENT)
        {
            if (!job_exists(to, JOB_PUSH))
                sync_set(to, 0);
            return 0;
        }
    }

    job_schedule(JOB_RENAME, from, 0, 0, to, NULL);

    return 0;
}

int op_releasedir(const char* path, struct fuse_file_info *fi)
{
    int res = 0;
    DIR **dirp;
    dirp = (DIR **)fi->fh;

    if (closedir(*dirp) == -1)
        res = -errno;

    dirp++;

    if (closedir(*dirp) == -1)
        res = -errno;

    worker_unblock();
    return res;
}

#define OP_OPEN 0
#define OP_CREATE 1
static int op_open_create(int op, const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int sync;
    int fh[FH_ELEMENTS], *fhp;
    char *pc, *pr;
    size_t p_len;

    p_len = strlen(path);

    FH_FLAGS(fh) = 0;

    if (ONLINE && !lock_has(path, LOCK_OPEN))
    {
        sync = sync_get(path);

        if (sync == -1)
        {
            return -EIO;
        }

        if (job_exists(path, JOB_PULL))
        {
            /* wait until current instant_pull finished */
            pthread_mutex_lock(&m_instant_pull);
            pthread_mutex_unlock(&m_instant_pull);

            job_delete(path, JOB_PULL);
            transfer_instant_pull(path);
        }
        else if (!job_exists(path, JOB_PUSH) && (sync == SYNC_NEW || sync == SYNC_MOD))
        {
            /* wait until eventually running instant_pull is finished */
            pthread_mutex_lock(&m_instant_pull);
            pthread_mutex_unlock(&m_instant_pull);

            transfer_instant_pull(path);
        }
        else if (sync == SYNC_CHG)
        {
            pc = cache_path2(path, p_len);
            pr = remote_path2(path, p_len);
            if (!pc || !pr)
            {
                free(pr), free(pc);
                return -EIO;
            }

            copy_attrs(pr, pc);

            free(pr), free(pc);
        }
    }

    pc = cache_path2(path, p_len);
    if (!pc)
    {
        return -EIO;
    }

    if (op == OP_OPEN)
        FH_FD(fh) = open(pc, fi->flags);
    else
        FH_FD(fh) = open(pc, fi->flags, mode);

    free(pc);

    /* open() failed */
    if (FH_FD(fh) == -1)
    {
        return -errno;
    }

    if (op == OP_CREATE)
    {
        if (ONLINE && !remoteop_create(path, fi->flags,  mode))
            sync_set(path, 0);
        else
            job_schedule(JOB_CREATE, path, fi->flags, mode, NULL, NULL);
    }

    lock_set(path, LOCK_OPEN);

    if ((fhp = malloc(sizeof fh)) == NULL)
    {
        close(FH_FD(fh));
        return -EIO;
    }

    memcpy(fhp, fh, sizeof fh);
    fi->fh = (uint64_t) fhp;
    return 0;
}

int op_open(const char *path, struct fuse_file_info *fi)
{
    return op_open_create(OP_OPEN, path, 0, fi);
}

int op_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return op_open_create(OP_CREATE, path, mode, fi);
}

int op_flush(const char *path, struct fuse_file_info *fi)
{
    int res;
    /* forward the flush op to underlying fd */
    res = close(dup(FI_FD(fi)));
    if (res == -1)
        return -errno;

    return 0;
}

int op_release(const char *path, struct fuse_file_info *fi)
{
    int res;
    char *p;
    struct stat st;
    lock_remove(path, LOCK_OPEN);
    res = close(FI_FD(fi));

    /* file written -> schedule push */
    if (FI_FLAGS(fi) & FH_WRITTEN)
    {
        p = cache_path(path);

        /* check first if file still exists */
        if (!lstat(p, &st))
            job_schedule_push(path);

        free(p);
    }
    free((int*)fi->fh);
    return res;
}

int op_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int res;

    if (isdatasync)
        res = fdatasync(FI_FD(fi));
    else
        res = fsync(FI_FD(fi));

    if (res == -1)
        return -errno;
    return 0;
}

int op_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int res;
    int fd;

    fd = dirfd((DIR *)fi->fh);

    if (isdatasync)
        res = fdatasync(fd);
    else
        res = fsync(fd);

    if (res == -1)
        return -errno;
    return 0;
}

int op_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res;

    res = pread(FI_FD(fi), (void *)buf, size, offset);

    if (res == -1)
        return -errno;
    return res;
}

int op_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res;

    res = pwrite(FI_FD(fi), (void *)buf, size, offset);

    if (res == -1)
        return -errno;

    FI_FLAGS(fi) |= FH_WRITTEN;
    return res;
}

int op_truncate(const char *path, off_t size)
{
    int res;
    char *p;
    size_t p_len = strlen(path);

    p = cache_path2(path, p_len);
    res = truncate(p, size);
    free(p);

    if (res == -1)
        return -errno;

    if (ONLINE)
    {
        if (!lock_has(path, LOCK_OPEN))
        {
            if (lock_has(path, LOCK_TRANSFER))
            {
                transfer_abort();
                lock_remove(path, LOCK_TRANSFER);
            }

            p = remote_path2(path, p_len);
            res = truncate(p, size);
            free(p);
            if (res == -1 && !job_exists(path, JOB_PUSH))
                return -errno;
        }
    }
    else
    {
        job_schedule_push(path);
    }

    return 0;
}

int op_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;
    char *p;

    p = cache_path(path);
    res = chown(p, uid, gid);
    free(p);

    if (res)
        return -errno;

    if (ONLINE)
    {
        res = remoteop_chown(path, uid, gid);
        if (!res)
        {
            sync_set(path, 0);
            return 0;
        }
    }

    job_schedule(JOB_CHOWN, path, uid, gid, NULL, NULL);

    return 0;
}

int op_chmod(const char *path, mode_t mode)
{
    int res;
    char *p;

    p = cache_path(path);
    res = chmod(p, mode);
    free(p);

    if (res)
        return -errno;

    if (ONLINE)
    {
        res = remoteop_chmod(path, mode);
        if (!res)
        {
            sync_set(path, 0);
            return 0;
        }
    }

    job_schedule(JOB_CHMOD, path, mode, 0, NULL, NULL);

    return 0;
}

int op_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    char *p;
    size_t p_len = strlen(path);

    p = cache_path2(path, p_len);
    res = utimensat(-1, p, ts, AT_SYMLINK_NOFOLLOW);
    free(p);
    if (res == -1)
        return -errno;

    if (ONLINE)
    {
        p = remote_path2(path, p_len);
        utimensat(-1, p, ts, AT_SYMLINK_NOFOLLOW);
        free(p);
    }

    return 0;
}

int op_statfs(const char *path, struct statvfs *buf)
{
    int res;
    char *p;

    p = (ONLINE) ? remote_path(path) : cache_path(path);
    res = statvfs(p, buf);
    free(p);

    if (res == -1)
        return -errno;
    return 0;
}

#if HAVE_SETXATTR
int op_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int res;
    char *p;

    if (!(discofs_options.fs_features & FEAT_XATTR))
        return -ENOTSUP;

    p = cache_path(path);
    if (!p)
        return -EIO;

    res = lsetxattr(p, name, value, size, flags);
    free(p);

    if (res)
        return -errno;

    if (ONLINE)
    {
        res = remoteop_setxattr(path, name, value, size, flags);
        if (!res)
        {
            sync_set(path, 0);
            return 0;
        }
    }

    job_schedule(JOB_SETXATTR, path, size, flags, name, value);

    return 0;
}

int op_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int res;
    char *p;

    if (!(discofs_options.fs_features & FEAT_XATTR))
        return -ENOTSUP;

    p = (ONLINE) ? remote_path(path) : cache_path(path);

    res = lgetxattr(p, name, value, size);
    free(p);

    if (res == -1)
        return -errno;
    return 0;
}

int op_listxattr(const char *path, char *list, size_t size)
{
    int res;
    char *p;

    if (!(discofs_options.fs_features & FEAT_XATTR))
        return -ENOTSUP;

    p = (ONLINE) ? remote_path(path) : cache_path(path);
    res = llistxattr(p, list, size);
    free(p);

    if (res == -1)
        return -errno;
    return 0;
}
#endif
