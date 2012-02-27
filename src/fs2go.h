/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_H
#define FS2GO_H
#include "config.h"

#include "log.h"

#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define STATE_ONLINE 0
#define STATE_OFFLINE 1
#define STATE_EXITING 2

#define ONLINE (get_state() == STATE_ONLINE)
#define OFFLINE (get_state() == STATE_OFFLINE)
#define EXITING (get_state() == STATE_EXITING)

#define CACHE_ROOT fs2go_options.cache_root
#define CACHE_ROOT_LEN fs2go_options.cache_root_len
#define REMOTE_ROOT fs2go_options.remote_root
#define REMOTE_ROOT_LEN fs2go_options.remote_root_len

#define TRANSFER_SIZE 4096

typedef long long rowid_t;

typedef int fs_feat_t;
#define FEAT_NS 1
#define FEAT_XATTR 2
#define FEAT_HARDLINKS 4

enum opt_conflict {
    CONFLICT_NEWER,
    CONFLICT_THEIRS,
    CONFLICT_MINE
};

#define COPYATTR_NO_MODE 1
#define COPYATTR_NO_OWNER 2
#define COPYATTR_NO_GROUP 4
#define COPYATTR_NO_XATTR 8

#define COPYATTR_SSHFS (COPYATTR_NO_OWNER | COPYATTR_NO_GROUP | COPYATTR_NO_XATTR)
#define COPYATTR_NFS (COPYATTR_NO_OWNER | COPYATTR_NO_GROUP)

#ifdef debug
#define DEF_LOGLEVEL LOG_DEBUG
#else
#define DEF_LOGLEVEL LOG_ERROR
#endif

#define DEF_SCAN_INTERVAL 10
#define DEF_CONFLICT CONFLICT_NEWER
#define DEF_COPYATTR 0
#define DEF_LOGFILE NULL

struct options {
    char *fs2go_mp;
    char *remote_root;
    size_t remote_root_len;
    char *data_root;
    char *cache_root;
    size_t cache_root_len;
    char *db_file;
    uid_t uid;
    gid_t gid;
    char *host;
    char *pid_file;
    char *backup_prefix;
    char *backup_suffix;
    int clear;
    int conflict;
    int copyattr;
    unsigned int scan_interval;
    int loglevel;
    char *logfile;
};

#define OPTIONS_INIT \
{   .fs2go_mp = NULL,\
    .remote_root = NULL,\
    .data_root = NULL,\
    .cache_root = NULL,\
    .db_file = NULL,\
    .uid = 0,\
    .gid = 0,\
    .host = NULL,\
    .pid_file = NULL,\
    .backup_prefix = NULL,\
    .backup_suffix = NULL,\
    .clear = 0,\
    .conflict = DEF_CONFLICT,\
    .copyattr = DEF_COPYATTR,\
    .scan_interval = DEF_SCAN_INTERVAL, \
    .loglevel = DEF_LOGLEVEL,\
    .logfile = DEF_LOGFILE }

enum fs2go_opt_keys
{
    FS2GO_OPT_HELP,
    FS2GO_OPT_VERSION,
    FS2GO_OPT_UID,
    FS2GO_OPT_GID,
    FS2GO_OPT_PID,
    FS2GO_OPT_CONFLICT,
    FS2GO_OPT_LOGLEVEL,
    FS2GO_OPT_DEBUG,
    FS2GO_OPT_FOREGROUND,
    FS2GO_OPT_NO_MODE,
    FS2GO_OPT_NO_OWNER,
    FS2GO_OPT_NO_GROUP,
    FS2GO_OPT_NO_XATTR,
    FS2GO_OPT_SSHFS,
    FS2GO_OPT_NFS,
};

int get_state();
void set_state(int s, int *oldstate);

int main(int argc, char **argv);
#endif
