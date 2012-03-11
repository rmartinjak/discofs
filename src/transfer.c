/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"

#include "transfer.h"
#include "job.h"
#include "lock.h"
#include "worker.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

extern struct options fs2go_options;

pthread_mutex_t m_instant_pull = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t m_transfer = PTHREAD_MUTEX_INITIALIZER;
static off_t t_off;
static char *t_path, *t_read = NULL, *t_write = NULL, *t_write_part = NULL;
static int t_active = 0, t_op = 0;

static void transfer_free(void);


static void transfer_free(void)
{
#define FREE(p) free(p); p = NULL;
    FREE(t_path);
    FREE(t_read);
    FREE(t_write);
    FREE(t_write_part);
#undef FREE
}

/* partially transfer file. if from and to are given, open files first */
int transfer(const char *from, const char *to)
{
#define CLOSE(fd) { if (close(fd)) PERROR("error closing fd"); }
    int fdread, fdwrite;
    ssize_t readbytes;
    char buf[TRANSFER_SIZE];
    int w_flags;

    if (from && to) {
        set_lock(t_path, LOCK_TRANSFER);
        VERBOSE("beginning transfer: '%s' -> '%s'\n", from, to);
        t_read = strdup(from);
        t_write = strdup(to);
        t_write_part = partfilename(t_write);

        t_active = 1;

        t_off = 0;
        w_flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (!t_active) {
        return TRANSFER_FAIL;
    }
    else {
        VERBOSE("resuming transfer: '%s' -> '%s' at %ld\n", t_read, t_write, t_off);
        w_flags = O_WRONLY | O_APPEND;
    }

    if (!t_read || !t_write) {
        ERROR("t_read or t_write is NULL\n");
        remove_lock(t_path, LOCK_TRANSFER);
        transfer_free();
        return TRANSFER_FAIL;
    }

    pthread_mutex_lock(&m_transfer);
    /* open files */
    if ((fdread = open(t_read, O_RDONLY)) == -1
            || lseek(fdread, t_off, SEEK_SET) == -1) {
        log_error(t_read);
        pthread_mutex_unlock(&m_transfer);
        transfer_abort();
        return TRANSFER_FAIL;
    }

    if ((fdwrite = open(t_write_part, w_flags, 0666)) == -1
            || lseek(fdwrite, t_off, SEEK_SET) == -1) {
        log_error(t_write_part);
        pthread_mutex_unlock(&m_transfer);
        transfer_abort();
        return TRANSFER_FAIL;
    }

    while (ONLINE && !worker_has_block()) {
        readbytes = read(fdread, buf, sizeof buf);
        if (readbytes < 0 || write(fdwrite, buf, readbytes) < readbytes || fsync(fdwrite)) {

            if (readbytes < 0)
                ERROR("failed to read from file\n");
            else
                ERROR("failed or incomplete write\n");

            pthread_mutex_unlock(&m_transfer);
            transfer_abort();
            return TRANSFER_FAIL;
        }

        /* copy completed, set mode and ownership */
        if (readbytes < sizeof buf) {

            t_active = 0;

            CLOSE(fdread);
            CLOSE(fdwrite);

            if (rename(t_write_part, t_write)) {
                PERROR("renaming after transfer");
            }

            copy_attrs(t_read, t_write);

            VERBOSE("transfer finished: '%s' -> '%s'\n", t_read, t_write);

            remove_lock(t_path, LOCK_TRANSFER);
            transfer_free();
            pthread_mutex_unlock(&m_transfer);

            return TRANSFER_FINISH;
        }
    }

    t_off = lseek(fdread, 0, SEEK_CUR);

    CLOSE(fdread);
    CLOSE(fdwrite);

    pthread_mutex_unlock(&m_transfer);
    return TRANSFER_OK;
#undef CLOSE
}

int transfer_begin(const struct job *j)
{
    int res;
    char *pread=NULL, *pwrite=NULL;
    size_t p_len = strlen(j->path);

    if (j->op == JOB_PUSH) {
        pread = cache_path2(j->path, p_len);
        pwrite = remote_path2(j->path, p_len);
    }
    else if (j->op == JOB_PULL) {
        pread = remote_path2(j->path, p_len);
        pwrite = cache_path2(j->path, p_len);
    }

    if (!pread || !pwrite) {
        free(pread);
        free(pwrite);
        errno = ENOMEM;
        return -1;
    }

    if (is_reg(pread)) {
        if (!is_reg(pwrite) && !is_nonexist(pwrite)) {
            DEBUG("write target is non-regular file: %s\n", pwrite);
            free(t_path);
            free(pread);
            free(pwrite);
            return TRANSFER_FAIL;
        }
        t_op = j->op;
        t_path = strdup(j->path);
        if (t_path)
            res = transfer(pread, pwrite);
        else {
            errno = ENOMEM;
            return -1;
        }
        free(pread);
        free(pwrite);
        return res;
    }
    /* if symlink, copy instantly */
    else if (is_lnk(pread)) {
        DEBUG("push/pull on symlink\n");
        copy_symlink(pread, pwrite);
        copy_attrs(pread, pwrite);
        free(t_path);
        free(pread);
        free(pwrite);
        return TRANSFER_FINISH;
    }
    else if (is_dir(pread)) {
        DEBUG("push/pull on DIR\n");
        clone_dir(pread, pwrite);
        copy_attrs(pread, pwrite);
        free(pread);
        free(pwrite);
        return TRANSFER_FINISH;
    }
    else {
        ERROR("cannot read file %s\n", pread);
        free(pread);
        free(pwrite);
        return TRANSFER_FAIL;
    }

    DEBUG("wtf\n");
    return TRANSFER_FAIL;
}

void transfer_rename_dir(const char *from, const char *to)
{
    size_t from_len;
    char *p, *t_path_new;

    if (!t_active)
        return;

    from_len = strlen(from);

    /* current transfer path doesn't begin with "from" -> nothing to do */
    if (strncmp(from, t_path, from_len))
        return;

    p = t_path;
    p += from_len;

    t_path_new = join_path(to, p);

    transfer_rename(t_path_new, 0);

    free(t_path_new);
}

void transfer_rename(const char *to, int rename_partfile)
{
    char *part_old;
    size_t to_len;

    if (!t_active)
        return;

    DEBUG("transfer_rename to %s\n", to);

    to_len = strlen(to);

    worker_block();
    pthread_mutex_lock(&m_transfer);

    remove_lock(t_path, LOCK_TRANSFER);
    free(t_path);
    t_path = strdup(to);
    set_lock(t_path, LOCK_TRANSFER);

    free(t_read);
    free(t_write);
    if (t_op == JOB_PUSH) {
        t_read = cache_path2(to, to_len);
        t_write = remote_path2(to, to_len);
    }
    else {
        t_read = remote_path2(to, to_len);
        t_write = cache_path2(to, to_len);
    }

    /* rename part file */
    part_old = t_write_part;
    t_write_part = partfilename(t_write);

    if (rename_partfile) {
        if (rename(part_old, t_write_part)) {
            PERROR("renaming partfile");
            ERROR("%s ->, %s\n", part_old, t_write_part);
            pthread_mutex_unlock(&m_transfer);
            transfer_abort();
            pthread_mutex_lock(&m_transfer);
        }
    }
    free(part_old);

    pthread_mutex_unlock(&m_transfer);
    worker_unblock();
}

void transfer_abort(void)
{
    if (!t_active)
        return;

    worker_block();
    pthread_mutex_lock(&m_transfer);

    t_active = 0;
    remove_lock(t_path, LOCK_TRANSFER);

    unlink(t_write_part);
    transfer_free();

    pthread_mutex_unlock(&m_transfer);
    worker_unblock();
}

/* instantly copy a file from remote to cache */
int transfer_instant_pull(const char *path)
{
    int res;
    char *pc;
    char *pr;
    size_t p_len = strlen(path);

    VERBOSE("instant_pulling %s\n", path);

    pthread_mutex_lock(&m_instant_pull);

    worker_block();

    pr = remote_path2(path, p_len);
    pc = cache_path2(path, p_len);

    /* copy data */
    res = copy_file(pr, pc);

    worker_unblock();

    copy_attrs(pr, pc);
    free(pr);
    free(pc);

    /* if copying failed, return error and dont set sync */
    if (res == -1) {
        ERROR("instant_pull on %s FAILED\n", path);
        pthread_mutex_unlock(&m_instant_pull);
        return -1;
    }

    /* file is in sync now */
    delete_jobs(path, JOB_PULL|JOB_PULLATTR);
    sync_set(path);

    pthread_mutex_unlock(&m_instant_pull);
    return 0;
}
