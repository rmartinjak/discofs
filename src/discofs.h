/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_H
#define DISCOFS_H
#include "config.h"

#include "log.h"

#include <stdbool.h>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>


/*=============*
 * DEFINITIONS *
 *=============*/

/* make options available for everybody including discofs.h */
extern struct options discofs_options;

#define CACHE_ROOT discofs_options.cache_root
#define CACHE_ROOT_LEN discofs_options.cache_root_len
#define REMOTE_ROOT discofs_options.remote_root
#define REMOTE_ROOT_LEN discofs_options.remote_root_len

#define TRANSFER_SIZE 4096

#define SLEEP_LONG 5
#define SLEEP_SHORT 2

/*--------------------*
 * remote fs features *
 *--------------------*/
#define FEAT_NS         (1 << 0)
#define FEAT_XATTR      (1 << 1)
#define FEAT_HARDLINKS  (1 << 2)

#define FS_FEAT(f) (discofs_options.fs_features & FEAT_##f)


/*---------------------*
 * attribute copy mask *
 *---------------------*/
#define COPYATTR_NO_MODE    (1 << 0)
#define COPYATTR_NO_OWNER   (1 << 1)
#define COPYATTR_NO_GROUP   (1 << 2)
#define COPYATTR_NO_XATTR   (1 << 3)

#define COPYATTR_SSHFS   (COPYATTR_NO_OWNER | COPYATTR_NO_GROUP | COPYATTR_NO_XATTR)
#define COPYATTR_NFS     (COPYATTR_NO_XATTR)


/*=========*
 * OPTIONS *
 *=========*/

/*-----------------*
 * default options *
 *-----------------*/
#define DEF_COPYATTR 0
#define DEF_LOGLEVEL LOG_ERROR
#define DEF_SCAN_INTERVAL 10
#define DEF_CONFLICT CONFLICT_NEWER


enum opt_conflict { CONFLICT_NEWER, CONFLICT_THEIRS, CONFLICT_MINE };

struct options
{
    char *discofs_mp;             /* discofs mount point */
    char *remote_root;          /* remote fs mount point */
    size_t remote_root_len;     /* string length of remote root */
    char *data_root;            /* directory for cache/db */
    char *cache_root;           /* cache root */
    size_t cache_root_len;      /* string length of cache root */
    bool debug;                 /* "-d" specified */
    int fs_features;            /* features of remote filesystem */
    uid_t uid;                  /* uid to setuid() to */
    gid_t gid;                  /* gid to setgid() to */
    char *host;                 /* host to PING for remote fs availability */
    char *pid_file;             /* PID file to check for remote fs availability */
    char *backup_prefix;        /* prefix for backups during conflict resolution */
    char *backup_suffix;        /* suffix ... */
    int conflict;               /* conflict resolution mode */
    int clear;                  /* delete database and cache before starting */
    int copyattr;               /* attribute copy mask */
    unsigned int scan_interval; /* interval between scan_remote() passes */
    int loglevel;               /* logging level */
    char *logfile;              /* log file name */
};

/* options initializer */
#define OPTIONS_INIT \
{   .discofs_mp = NULL,\
    .remote_root = NULL,\
    .data_root = NULL,\
    .cache_root = NULL,\
    .debug = false,\
    .fs_features = 0,\
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
    .logfile = NULL }


/* option keys for discofs_opt_proc */
enum discofs_opt_keys
{
    DISCOFS_OPT_HELP,
    DISCOFS_OPT_VERSION,
    DISCOFS_OPT_UID,
    DISCOFS_OPT_GID,
    DISCOFS_OPT_PID,
    DISCOFS_OPT_CONFLICT,
    DISCOFS_OPT_LOGLEVEL,
    DISCOFS_OPT_DEBUG,
    DISCOFS_OPT_FOREGROUND,
    DISCOFS_OPT_NO_MODE,
    DISCOFS_OPT_NO_OWNER,
    DISCOFS_OPT_NO_GROUP,
    DISCOFS_OPT_NO_XATTR,
    DISCOFS_OPT_SSHFS,
    DISCOFS_OPT_NFS,
};

#endif
