/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "hardlink.h"

#include "sync.h"
#include "db.h"
#include "queue.h"

#include <sys/types.h>


int hardlink_sync_set(ino_t inode)
{
    int res;
    char *path;
    queue *q = q_init();

    res = db_hardlink_get(inode, q);
    if (res != DB_OK)
    {
        q_free(q, free);
        return -1;
    }

    res = 0;
    while (!res && (path = q_dequeue(q)))
    {
        /* NOHARDLINKS flag prevents infinite recursion */
        res = sync_set(path, SYNC_NOHARDLINKS);
        if (res)
            break;

        res = job_delete(path, JOB_PULL);
    }

    q_free(q, free);

    return res;
}

int hardlink_add(const char *path, ino_t inode)
{
    int res;

    res = db_hardlink_add(path, inode);

    return res;
}

int hardlink_create(const char *path, ino_t inode)
{
    int res;
    char *p, *oldpath, *newpath;
    struct stat st;
    queue *q;

    newpath = cache_path(path);
    q = q_init();

    if (!newpath || !q || db_hardlink_get(inode, q) != DB_OK)
    {
        free(newpath);
        q_free(q, free);
        return -1;
    }

    /* try all stored names (usually the first attempt should succeed) */
    while ((p = q_dequeue(q)))
    {
        oldpath = cache_path(p);
        if (!oldpath || lstat(oldpath, &st))
        {
            free(newpath);
            q_free(q, free);
            return -1;
        }

        res = link(oldpath, newpath);

        free(oldpath);
        free(p);

        if (!res)
        {
            free(newpath);
            q_free(q, free);
            hardlink_add(path, inode);
            return 0;
        }
        else
        {
            PERROR("link() in hardlink_create");
        }
    }

    free(newpath);
    q_free(q, NULL);
    return -1;
}

int hardlink_remove(const char *path)
{
    int res;

    res = db_hardlink_remove(path);

    return res;
}

int hardlink_rename_dir(const char *from, const char *to)
{
    if (db_hardlink_rename_dir(from, to) != DB_OK)
        return -1;
    return 0;
}

int hardlink_rename_file(const char *from, const char *to)
{
    if (db_hardlink_rename_file(from, to) != DB_OK)
        return -1;
    return 0;
}
