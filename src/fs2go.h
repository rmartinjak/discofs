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


/*=============*/
/* DEFINITIONS */
/*=============*/

/* make options available for everybody including fs2go.h */
extern struct options fs2go_options;

#define CACHE_ROOT fs2go_options.cache_root
#define CACHE_ROOT_LEN fs2go_options.cache_root_len
#define REMOTE_ROOT fs2go_options.remote_root
#define REMOTE_ROOT_LEN fs2go_options.remote_root_len

#define TRANSFER_SIZE 4096


/*--------------------*/
/* remote fs features */
/*--------------------*/
#define FEAT_NS 1
#define FEAT_XATTR 2
#define FEAT_HARDLINKS 4

#define FS_FEAT(f) (fs2go_options.fs_features & FEAT_##f)


/*---------------------*/
/* attribute copy mask */
/*---------------------*/
#define COPYATTR_NO_MODE    1
#define COPYATTR_NO_OWNER   2
#define COPYATTR_NO_GROUP   4
#define COPYATTR_NO_XATTR   8

#define COPYATTR_SSHFS   (COPYATTR_NO_OWNER | COPYATTR_NO_GROUP | COPYATTR_NO_XATTR)
#define COPYATTR_NFS     (COPYATTR_NO_XATTR)


/*=========*/
/* OPTIONS */
/*=========*/

/*-----------------*/
/* default options */
/*-----------------*/
#define DEF_COPYATTR 0
#define DEF_LOGLEVEL LOG_ERROR
#define DEF_SCAN_INTERVAL 10
#define DEF_CONFLICT CONFLICT_NEWER


enum opt_conflict { CONFLICT_NEWER, CONFLICT_THEIRS, CONFLICT_MINE };

struct options
{
    char *fs2go_mp;             /* fs2go mount point */
    char *remote_root;          /* remote fs mount point */
    size_t remote_root_len;     /* string length of remote root */
    char *data_root;            /* directory for cache/db */
    char *cache_root;           /* cache root */
    size_t cache_root_len;      /* string length of cache root */
    int debug;                  /* "-d" specified */
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
{   .fs2go_mp = NULL,\
    .remote_root = NULL,\
    .data_root = NULL,\
    .cache_root = NULL,\
    .debug = 0,\
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


/* option keys for fs2go_opt_proc */
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

#endif
