/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "transfer.h"

#include "state.h"
#include "job.h"
#include "sync.h"
#include "lock.h"
#include "worker.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>


pthread_mutex_t m_instant_pull = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t m_transfer = PTHREAD_MUTEX_INITIALIZER;

static struct transfer_state
{
    struct job *job;
    char *read_path, *write_path;
    bool active;
    off_t offset;
} t_state;

static void transfer_reset_state(void);
static int transfer_pull_dir(const char *path);

static void transfer_reset_state(void)
{
    pthread_mutex_lock(&m_transfer);
    free(t_state.read_path);
    free(t_state.write_path);

    t_state.active = false;
    t_state.job = NULL;
    t_state.read_path = NULL;
    t_state.write_path = NULL;
    t_state.offset = 0;
    pthread_mutex_unlock(&m_transfer);
}

static int transfer_pull_dir(const char *path)
{
    int res;
    char *pr, *pc;

    if (!strcmp(path, "/"))
        return -1;

    pr = remote_path(path);
    pc = cache_path(path);

    if (!pr || !pc)
    {
        free(pr);
        free(pc);
        return -1;
    }

    res = clone_dir(pr, pc);

    if (res && errno == ENOENT)
    {
        int res2;
        char *parent = dirname_r(path);

        if (parent)
        {
            res2 = transfer_pull_dir(parent);
            free(parent);

            if (!res2)
                res = clone_dir(pr, pc);
        }
    }

    free(pr);
    free(pc);

    return res;
}

int transfer(const char *from, const char *to)
{
#define CLOSE(fd) { if (close(fd)) PERROR("error closing fd"); }
    int fdread, fdwrite;
    ssize_t readbytes;
    char buf[TRANSFER_SIZE];
    int w_flags;

    pthread_mutex_lock(&m_transfer);

    if (from && to)
    {
        lock_set(t_state.job->path, LOCK_TRANSFER);
        VERBOSE("beginning transfer: '%s' -> '%s'\n", from, to);
        t_state.read_path = strdup(from);
        t_state.write_path = strdup(to);

        w_flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (!t_state.active)
    {
        pthread_mutex_unlock(&m_transfer);
        return TRANSFER_FINISH;
    }
    else
    {
        VERBOSE("resuming transfer: '%s' -> '%s' at %ld\n",
                t_state.read_path, t_state.write_path, t_state.offset);

        w_flags = O_WRONLY | O_APPEND;
    }


    if (!t_state.read_path || !t_state.write_path)
    {
        ERROR("t_state.read_path or t_state.write_path is NULL\n");
        lock_remove(t_state.job->path, LOCK_TRANSFER);
        goto failure;
    }

    /* open files */
    if ((fdread = open(t_state.read_path, O_RDONLY)) == -1
            || lseek(fdread, t_state.offset, SEEK_SET) == -1) {
        PERROR(t_state.read_path);
        goto failure;
    }

    if ((fdwrite = open(t_state.write_path, w_flags, 0666)) == -1
            || lseek(fdwrite, t_state.offset, SEEK_SET) == -1) {
        PERROR(t_state.write_path);
        goto failure;
    }

    while (ONLINE && !worker_blocked())
    {
        readbytes = read(fdread, buf, sizeof buf);
        if (readbytes && (readbytes < 0 || write(fdwrite, buf, readbytes) < readbytes || fsync(fdwrite)))
        {

            if (readbytes < 0)
                ERROR("failed to read from file\n");
            else
                ERROR("failed or incomplete write\n");

            goto failure;
        }

        /* copy completed, set mode and ownership */
        if (readbytes < sizeof buf)
        {
            CLOSE(fdread);
            CLOSE(fdwrite);

            copy_attrs(t_state.read_path, t_state.write_path);

            VERBOSE("transfer finished: '%s' -> '%s'\n", t_state.read_path, t_state.write_path);

            lock_remove(t_state.job->path, LOCK_TRANSFER);
            pthread_mutex_unlock(&m_transfer);
            transfer_reset_state();

            return TRANSFER_FINISH;
        }
    }

    t_state.offset = lseek(fdread, 0, SEEK_CUR);

    CLOSE(fdread);
    CLOSE(fdwrite);

    pthread_mutex_unlock(&m_transfer);
    return TRANSFER_OK;

failure:
    pthread_mutex_unlock(&m_transfer);
    transfer_abort();
    return TRANSFER_FAIL;
#undef CLOSE
}

int transfer_begin(struct job *j)
{
    int res;
    char *pread = NULL, *pwrite = NULL;
    size_t p_len;

    pthread_mutex_lock(&m_transfer);

    if (t_state.active)
    {
        DEBUG("called transfer_begin while a transfer is active!\n");
        pthread_mutex_unlock(&m_transfer);
        return TRANSFER_FAIL;
    }
    pthread_mutex_unlock(&m_transfer);

    p_len = strlen(j->path);

    if (j->op == JOB_PUSH)
    {
        pread = cache_path2(j->path, p_len);
        pwrite = remote_path2(j->path, p_len);
    }
    else if (j->op == JOB_PULL)
    {
        pread = remote_path2(j->path, p_len);
        pwrite = cache_path2(j->path, p_len);
    }

    if (!pread || !pwrite)
    {
        free(pread);
        free(pwrite);
        errno = ENOMEM;
        return -1;
    }

    if (is_reg(pread))
    {
        if (!is_reg(pwrite) && !is_nonexist(pwrite))
        {
            DEBUG("write target is non-regular file: %s\n", pwrite);
            free(pread);
            free(pwrite);
            return TRANSFER_FAIL;
        }

        pthread_mutex_lock(&m_transfer);
        t_state.active = true;
        t_state.job = j;
        t_state.offset = 0;
        pthread_mutex_unlock(&m_transfer);

        res = transfer(pread, pwrite);
        free(pread);
        free(pwrite);
        return res;
    }
    /* if symlink, copy instantly */
    else if (is_lnk(pread))
    {
        DEBUG("push/pull on symlink\n");
        copy_symlink(pread, pwrite);
        copy_attrs(pread, pwrite);
        free(pread);
        free(pwrite);
        return TRANSFER_FINISH;
    }
    else if (is_dir(pread))
    {
        DEBUG("push/pull on DIR\n");
        clone_dir(pread, pwrite);
        copy_attrs(pread, pwrite);
        free(pread);
        free(pwrite);
        return TRANSFER_FINISH;
    }
    else
    {
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

    if (!t_state.active)
        return;

    from_len = strlen(from);

    /* current transfer path doesn't begin with "from" -> nothing to do */
    if (strncmp(from, t_state.job->path, from_len))
        return;

    p = t_state.job->path;
    p += from_len;

    t_path_new = join_path(to, p);

    transfer_rename(t_path_new);

    free(t_path_new);
}

void transfer_rename(const char *to)
{
    size_t to_len;

    pthread_mutex_lock(&m_transfer);

    if (!t_state.active)
    {
        pthread_mutex_unlock(&m_transfer);
        return;
    }

    DEBUG("transfer_rename to %s\n", to);

    to_len = strlen(to);

    worker_block();

    lock_remove(t_state.job->path, LOCK_TRANSFER);
    free(t_state.job->path);
    t_state.job->path = strdup(to);
    lock_set(t_state.job->path, LOCK_TRANSFER);

    free(t_state.read_path);
    free(t_state.write_path);
    if (t_state.job->op == JOB_PUSH)
    {
        t_state.read_path = cache_path2(to, to_len);
        t_state.write_path = remote_path2(to, to_len);
    }
    else
    {
        t_state.read_path = remote_path2(to, to_len);
        t_state.write_path = cache_path2(to, to_len);
    }

    pthread_mutex_unlock(&m_transfer);
    worker_unblock();
}

void transfer_abort(void)
{
    worker_block();
    pthread_mutex_lock(&m_transfer);

    if (t_state.active)
    {
        lock_remove(t_state.job->path, LOCK_TRANSFER);
        unlink(t_state.write_path);

        pthread_mutex_unlock(&m_transfer);
        transfer_reset_state();
    }

    worker_unblock();
    pthread_mutex_unlock(&m_transfer);
}

int transfer_instant_pull(const char *path)
{
    int res;
    char *pc, *pr;
    size_t p_len = strlen(path);
    bool path_equal = false;

    VERBOSE("instant_pulling %s\n", path);

    pthread_mutex_lock(&m_instant_pull);

    worker_block();

    pr = remote_path2(path, p_len);
    pc = cache_path2(path, p_len);

    pthread_mutex_lock(&m_transfer);
    if (t_state.active)
        path_equal = !strcmp(path, t_state.job->path);
    pthread_mutex_unlock(&m_transfer);

    /* requested file is already being transfered (normally).
       just continue the transfer until it is finished */
    if (path_equal)
    {
        /* continuing a running transfer() only works if !worker_blocked() */
        worker_unblock();
        do
        {
            res = transfer(NULL, NULL);
        }
        while (ONLINE && res == TRANSFER_OK);

        res = (res == TRANSFER_FINISH) ? 0 : 1;
        worker_block();
    }
    else
    {
        res = copy_file(pr, pc);

        /* if copy_file failed, possibly because the file's directory didn't
           exist in the cache yet. create it and retry */
        if (res && errno == ENOENT)
        {
            char *dir = dirname_r(path);

            if (dir)
            {
                transfer_pull_dir(dir);
                free(dir);
                res = copy_file(pr, pc);
            }
        }
    }

    worker_unblock();

    copy_attrs(pr, pc);
    free(pr);
    free(pc);

    /* if copying failed, return error and dont set sync */
    if (res)
    {
        ERROR("instant_pull on %s FAILED\n", path);
        pthread_mutex_unlock(&m_instant_pull);
        return -1;
    }

    /* file is in sync now */
    job_delete(path, JOB_PULL);
    sync_set(path, 0);

    pthread_mutex_unlock(&m_instant_pull);
    return 0;
}
