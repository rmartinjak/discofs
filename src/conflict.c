/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "conflict.h"

#include "fs2go.h"
#include "job.h"
#include "db.h"
#include "funcs.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

extern struct options fs2go_options;

int conflict_handle(const char *path, job_op op, int *keep_which)
{
    int res;
    int keep;
    char *p;
    size_t p_len = path;

    /* select which to keep by comparing mtime */
    if (fs2go_options.conflict == CONFLICT_NEWER) {
        struct stat st_c;
        struct stat st_r;
        int cmp;

        /* stat cache file */
        p = cache_path(path, p_len);
        res = lstat(p, &st_c);
        free(p);
        if (res == -1)
            return -1;

        /* stat remote file */
        p = remote_path(path, p_len);
        res = lstat(p, &st_r);
        free(p);
        if (res == -1)
            return -1;

        /* compare mtime */
        cmp = timecmp(ST_MTIME(st_c), ST_MTIME(st_r));

        /* keep newer file */
        keep = (cmp < 0) ? CONFLICT_KEEP_REMOTE : CONFLICT_KEEP_CACHE;
    }
    /* always keep remote */
    else if (fs2go_options.conflict == CONFLICT_THEIRS)
        keep = CONFLICT_KEEP_REMOTE;
    /* always keep cache */
    else if (fs2go_options.conflict == CONFLICT_MINE)
        keep = CONFLICT_KEEP_CACHE;

    /* save which file to keep in caller-provided pointer */
    if (keep_which)
        *keep_which = keep;

    /* delete/backup the file NOT to keep
       this will also schedule a push/pull job if
       the file was backed up */
    delete_or_backup(path, keep);

    if (keep == CONFLICT_KEEP_REMOTE) {
        p = cache_path(path, p_len);

        if (op == JOB_RENAME) {
            char *newpath = conflict_path(path);
            /* no prefix/suffix -> delete sync and sync/jobs in db */
            if (!newpath) {
                if (is_dir(p))
                    sync_delete_dir(path);
                else
                    sync_delete_file(path);
                db_delete_path(path);
            }
            /* else rename sync and sync/jobs in db */
            else if (is_dir(p)) {
                sync_rename_dir(path, newpath);
                db_rename_dir(path, newpath);
            }
            else {
                sync_rename_file(path, newpath);
                db_rename_file(path, newpath);
            }
            free(newpath);
        }
        /* PUSH job -> pull the file from remote */
        else
            schedule_pull(path);
        free(p);
    }

    return 0;
}

char *conflict_path(const char *path)
{
    if (fs2go_options.backup_prefix || fs2go_options.backup_suffix)
        return affix_filename(path, fs2go_options.backup_prefix, fs2go_options.backup_suffix);

    return NULL;
}

int delete_or_backup(const char *path, int keep)
{
    int res;
    char *p, *confp;
    char *dest;

    /* get old path */
    p = (keep == CONFLICT_KEEP_REMOTE) ? cache_path(path, strlen(path)) : remote_path(path, strlen(path));

    /* get new path (or NULL if no bprefix/bsuffix set) */
    dest = conflict_path(p);

    if (dest) {
        /* move to backup filename instead of deleting */
        /* rename */
        res = rename(p, dest);
        free(dest);

        /* schedule push/pull for new file */
        if (!res) {
            confp = conflict_path(path);
            if (keep == CONFLICT_KEEP_REMOTE)
                schedule_push(confp);
            else
                schedule_pull(confp);
            free(confp);
        }
    }
    /* no backup prefix or suffix set -> just delete */
    else {

        if (is_dir(path))
            res = rmdir_rec(p);
        else
            res = unlink(p);
    }

    free(p);
    return res;
}
