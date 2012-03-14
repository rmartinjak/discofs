/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"

#include "fs2go.h"

#include "log.h"
#include "funcs.h"
#include "sync.h"
#include "job.h"
#include "worker.h"
#include "db.h"
#include "paths.h"

#ifdef DEBUG_FS_OPS
#include "debugops.h"
#else
#include "fsops.h"
#endif

#include <fuse.h>
#include <fuse_opt.h>

#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>

#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif

/* the current state. can be STATE_ONLINE, STATE_OFFLINE or STATE_EXITING */
static int fs2go_state = STATE_OFFLINE;

/* mutex for getting/setting the state */
static pthread_mutex_t m_fs2go_state = PTHREAD_MUTEX_INITIALIZER;

/* mount options (specified i.e. with "-o name,name=value..." */
struct options fs2go_options = OPTIONS_INIT;


/* static prototypes */
static void print_usage();
static void print_version();
static void log_options(int loglevel, struct options opt);
static int fs2go_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs);
static int test_fs_features(int *f);


/* returns the current state */
int get_state()
{
    int s;

    pthread_mutex_lock(&m_fs2go_state);

    s = fs2go_state;

    pthread_mutex_unlock(&m_fs2go_state);

    return s;
}

/* set the new state; this has no effect when current state is STATE_EXITING */
void set_state(int s, int *oldstate)
{
    /* put old state in caller-provided pointer */
    if (oldstate)
        *oldstate = fs2go_state;

    /* don't change status when exiting */
    if (fs2go_state == STATE_EXITING)
        return;

    pthread_mutex_lock(&m_fs2go_state);

    /* log new state if it differs from old state */
    if (s == STATE_ONLINE && s != fs2go_state)
        VERBOSE("changing state to ONLINE\n");
    else if (s == STATE_OFFLINE && s != fs2go_state)
        VERBOSE("changing state to OFFLINE\n");
    else if (s == STATE_EXITING && s != fs2go_state)
        VERBOSE("changing state to EXITING\n");

    fs2go_state = s;

    pthread_mutex_unlock(&m_fs2go_state);
}

static void print_usage()
{
    char *s = "usage: " PROG_NAME " [ -hvdf ] [ -o option[,option]...] remote_fs mountpoint\n"
        "\n"
        "general options:\n"
        /*"  -o opt,[opt...]    mount options\n"*/
        " -h --help             display help and exit\n"
        " -v --version          display version and exit\n"
        " -d --debug            enable debugging output, don't fork to background\n"
        " -f --foreground       don't fork to background\n"
        "\n"
        PROG_NAME " options:\n"
        " data=<dir>            directory for database and cache\n"
        " host=<host>           hostname or IP address to PING for remote fs availability\n"
        " pid=<filename>        file containing PID to test for remote fs avialability\n"
        " scan=<seconds>        interval to wait before scanning remote fs for changes. default is " STR(DEF_SCAN_INTERVAL) "\n"
        " conflict=<mode>       conflict resolution mode. possible values:\n"
        "                       'newer', 'mine' or 'theirs'. default is 'newer'\n"
        " bprefix=<prefix>\n"
        " bsuffix=<suffix>      backup prefix/suffix (see the manual for more information)\n"
        " clear                 delete database and cache before mounting\n"
        " loglevel=<level>      logging level, possible values: none, error, info, verbose, debug, fsop\n"
        "                       each including its predecessors. default is 'none'\n"
        " logfile=<file>        logging output file. default ist stderr\n"
        "\n"
        "filesystem specific options:\n"
        " no-mode               don't sync access permissions\n"
        " no-owner              don't sync user ownership\n"
        " no-group              don't sync group ownership\n"
#if HAVE_SETXATTR
        " no-xattr              don't sync extended attributes\n"
        " sshfs                 same as \"no-owner,no-group,no-xattr\"\n"
        " nfs                   same as \"no-xattr\"\n"
#else
        " sshfs                 same as \"no-owner,no-group\"\n"
#endif
        "";

    fprintf(stderr, "%s", s);
}

static void print_version()
{
    printf("%s Version %s\n", PROG_NAME, PROG_VERSION);
}


/* log the options with which fs2go will be run */
static void log_options(int loglevel, struct options opt)
{
    const char *tmp;
#define YESNO(x) (x) ? "yes" : "no"
    log_print(loglevel, "fs2go options:\n");
    log_print(loglevel, "mount point: %s\n", opt.fs2go_mp);
    log_print(loglevel, "remote fs: %s\n", opt.remote_root);
    log_print(loglevel, "cache root: %s\n", opt.cache_root);
    log_print(loglevel, "debug: %s\n", YESNO(opt.clear));
    log_print(loglevel, "remote host: %s\n", opt.host);
    log_print(loglevel, "uid: %d\n", opt.uid);
    log_print(loglevel, "gid: %d\n", opt.gid);
    log_print(loglevel, "pid file: %s\n", opt.pid_file);
    log_print(loglevel, "backup prefix: %s\n", opt.backup_prefix);
    log_print(loglevel, "backup suffix: %s\n", opt.backup_suffix);
    log_print(loglevel, "clear: %s\n", YESNO(opt.clear));

    switch (opt.conflict) {
        case CONFLICT_NEWER:
            tmp = "newer";
            break;
        case CONFLICT_THEIRS:
            tmp = "theirs";
            break;
        case CONFLICT_MINE:
            tmp = "mine";
            break;
    }
    log_print(loglevel, "conflict: %s\n", tmp);

    log_print(loglevel, "no-mode: %s\n", YESNO((opt.copyattr & COPYATTR_NO_MODE)));
    log_print(loglevel, "no-owner: %s\n", YESNO((opt.copyattr & COPYATTR_NO_OWNER)));
    log_print(loglevel, "no-group: %s\n", YESNO((opt.copyattr & COPYATTR_NO_GROUP)));
#if HAVE_SETXATTR
    log_print(loglevel, "no-xattr: %s\n", YESNO((opt.copyattr & COPYATTR_NO_XATTR)));
#endif

    log_print(loglevel, "remote fs features:\n");
    log_print(loglevel, "nanosecond timestamps: %s\n", YESNO((fs2go_options.fs_features & FEAT_NS)));
#if HAVE_SETXATTR
    log_print(loglevel, "extended attributes: %s\n", YESNO((fs2go_options.fs_features & FEAT_XATTR)));
#endif
}

/* macro to define simple options */
#define OPT_KEY(t, p, v) { t, offsetof(struct options, p), v }

/* options recognized, anything else will be passed to FUSE */
static struct fuse_opt fs2go_opts[] =
{
    /* user or group ID to change to before mounting */
    FUSE_OPT_KEY("uid=%s", FS2GO_OPT_UID),
    FUSE_OPT_KEY("gid=%s", FS2GO_OPT_GID),

    /* alternate directory for database and cache */
    OPT_KEY("data=%s", data_root, 0),

    /* host to PING */
    OPT_KEY("host=%s", host, 0),

    /* PID file to check */
    OPT_KEY("pid=%s", pid_file, 0),

    /* interval to wait before scanning remote fs for changes */
    OPT_KEY("scan=%u", scan_interval, 0),

    /* conflict resolution mode */
    FUSE_OPT_KEY("conflict=%s", FS2GO_OPT_CONFLICT),

    /* backup prefix/sufix */
    OPT_KEY("bprefix=%s", backup_prefix, 0),
    OPT_KEY("bsuffix=%s", backup_suffix, 0),

    /* start with a fresh db and cache */
    OPT_KEY("clear", clear, 1),

    /* logging */
    FUSE_OPT_KEY("loglevel=%s", FS2GO_OPT_LOGLEVEL),
    OPT_KEY("logfile=%s", logfile, 0),

    /* file attributes not to copy */
    FUSE_OPT_KEY("no-mode", FS2GO_OPT_NO_MODE),
    FUSE_OPT_KEY("no-owner", FS2GO_OPT_NO_OWNER),
    FUSE_OPT_KEY("no-group", FS2GO_OPT_NO_GROUP),
#if HAVE_SETXATTR
    FUSE_OPT_KEY("no-xattr", FS2GO_OPT_NO_XATTR),
#endif
    /* preset combinations of the above suitable for certain fses */
    FUSE_OPT_KEY("sshfs", FS2GO_OPT_SSHFS),
    FUSE_OPT_KEY("nfs", FS2GO_OPT_NFS),

    /* generic stuff */
    FUSE_OPT_KEY("-v", FS2GO_OPT_VERSION),
    FUSE_OPT_KEY("--version", FS2GO_OPT_VERSION),
    FUSE_OPT_KEY("-h", FS2GO_OPT_HELP),
    FUSE_OPT_KEY("--help", FS2GO_OPT_HELP),
    FUSE_OPT_KEY("-d", FS2GO_OPT_DEBUG),
    FUSE_OPT_KEY("--debug", FS2GO_OPT_DEBUG),
    FUSE_OPT_KEY("-f", FS2GO_OPT_FOREGROUND),
    FUSE_OPT_KEY("--foreground", FS2GO_OPT_FOREGROUND),
    FUSE_OPT_END,
};
#undef OPT_KEY

static int fs2go_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    const char *val = NULL;
    char *p, *p2;
    char *endptr;
    struct passwd *pw;
    struct group *gr;

    switch (key) {

        /*==============*/
        /* MOUNT POINTS */
        /*==============*/

        case FUSE_OPT_KEY_NONOPT:
            /*--------------------*/
            /* remote mount point */
            /*--------------------*/
            if (!fs2go_options.remote_root)
            {

                /* transform to absolute path by appending the current
                   working directory if necessary */
                if (*arg != '/')
                {

                    /* allocate a huge buffer */
                    if ((p = malloc(PATH_MAX+1)) == NULL)
                        FATAL("memory allocation failed\n");

                    /* put current dir at the beginning */
                    getcwd(p, PATH_MAX+1);

                    /* add path separator + passed argument */
                    strcat(p, "/");
                    strcat(p, arg);
                }
                else
                {
                    /* only create a copy */
                    if ((p = strdup(arg)) == NULL)
                        FATAL("memory allocation failed\n");
                }

                /* cut off trailing slash */
                if (p[strlen(p)-1] == '/')
                    p[strlen(p)-1] = '\0';

                /* verify if it's a directory */
                if (!is_dir(p))
                {
                    fprintf(stderr, "remote mount point \"%s\" is not a directory\n", p);
                    exit(EXIT_FAILURE);
                }

                /* set fsname for a more descriptive output of "mount" */
                if ((p2 = malloc((strlen("-ofsname=") + strlen(p) + 1))))
                {
                    strcpy(p2, "-ofsname=");
                    strcat(p2, p);
                    fuse_opt_add_arg(outargs, p2);
                }

                /* get length of remote mount point */
                REMOTE_ROOT_LEN = strlen(p);

                /* save remote root as copy of p */
                if ((REMOTE_ROOT = malloc((REMOTE_ROOT_LEN+1))) == NULL)
                    FATAL("memory allocation failed\n");
                memcpy(REMOTE_ROOT, p, REMOTE_ROOT_LEN+1);

                /* free the temporary buffer */
                free(p);

                return 0;
            }

            /*-------------------*/
            /* fs2go mount point */
            /*-------------------*/

            /* second one is "our" mount point */
            else if (!fs2go_options.fs2go_mp)
            {
                fs2go_options.fs2go_mp = strdup(arg);
                fuse_opt_add_arg(outargs, arg);
                return 0;
            }
            return 1;


        /*======================*/
        /* --VERSION AND --HELP */
        /*======================*/

        case FS2GO_OPT_VERSION:
            print_version();
            exit(EXIT_SUCCESS);

        case FS2GO_OPT_HELP:
            print_usage();
            exit(EXIT_SUCCESS);

        /*==========================*/
        /* --DEBUG AND --FOREGROUND */
        /*==========================*/
        case FS2GO_OPT_DEBUG:
            fs2go_options.debug = 1;
            /* forward argument to fuse */
            fuse_opt_add_arg(outargs, "-d");
            return 0;

        case FS2GO_OPT_FOREGROUND:
            /* forward argument to fuse */
            fuse_opt_add_arg(outargs, "-f");
            return 0;


        /*================*/
        /* UID AND/OR GID */
        /*================*/

        /*-----*/
        /* UID */
        /*-----*/
        case FS2GO_OPT_UID:
            /* let val point to beginning of the actual uid string */
            val = arg + strlen("uid=");

            /* try to convert uid to number */
            fs2go_options.uid = (uid_t)strtol(val, &endptr, 10);


            /* get passwd entry for given uid */

            /* use numeric id if strtol() was successful */
            if (*endptr == '\0' && endptr != val)
                pw = getpwuid(fs2go_options.uid);

            /* or try the given string */
            else
                pw = getpwnam(val);

            /* no passwd entry found, print error and exit */
            if (!pw)
                FATAL("could not find user \"%s\"\n", val);

            /* set uid in options */
            fs2go_options.uid = pw->pw_uid;

            /* set gid to user's primary group if gid not already set */
            if (!fs2go_options.gid)
                fs2go_options.gid = pw->pw_gid;
            return 0;

        /*-----*/
        /* GID */
        /*-----*/
        case FS2GO_OPT_GID:
            val = arg + strlen("gid=");
            fs2go_options.gid = (gid_t)strtol(val, &endptr, 10);
            if (*endptr != '\0')
            {
                if (!(gr = getgrnam(val)))
                    FATAL("could not find group \"%s\"\n", val);
                fs2go_options.uid = gr->gr_gid;
            }
            return 0;


        /*=============================*/
        /* FILE ATTRIBUTES NOT TO COPY */
        /*=============================*/

        #define OPT_COPYADDR(n) case FS2GO_OPT_ ## n: \
            fs2go_options.copyattr |= COPYATTR_ ## n; \
            return 0

            OPT_COPYADDR(NO_MODE);
            OPT_COPYADDR(NO_OWNER);
            OPT_COPYADDR(NO_GROUP);
            OPT_COPYADDR(NO_XATTR);
            OPT_COPYADDR(NFS);
            OPT_COPYADDR(SSHFS);
        #undef OPT_COPYADDR


        /*==========*/
        /* LOGLEVEL */
        /*==========*/

        case FS2GO_OPT_LOGLEVEL:
            val = arg + strlen("loglevel=");
            if (!strcmp(val, "error"))
                fs2go_options.loglevel = LOG_ERROR;
            else if (!strcmp(val, "info"))
                fs2go_options.loglevel = LOG_INFO;
            else if (!strcmp(val, "verbose"))
                fs2go_options.loglevel = LOG_VERBOSE;
            else if (!strcmp(val, "fsop"))
                fs2go_options.loglevel = LOG_FSOP;
            else if (!strcmp(val, "debug"))
                fs2go_options.loglevel = LOG_DEBUG;
            else
            {
                print_usage();
                exit(EXIT_FAILURE);
            }
            return 0;


        /*==========================*/
        /* CONFLICT RESOLUTION MODE */
        /*==========================*/

        case FS2GO_OPT_CONFLICT:
            val = arg + strlen("conflict=");

            if (!strcmp(val, "newer") || !strcmp(val, "n"))
                fs2go_options.conflict = CONFLICT_NEWER;
            else if (!strcmp(val, "theirs") || !strcmp(val, "t"))
                fs2go_options.conflict = CONFLICT_THEIRS;
            else if (!strcmp(val, "mine") || !strcmp(val, "m"))
                fs2go_options.conflict = CONFLICT_MINE;
            else
            {
                print_usage();
                exit(EXIT_FAILURE);
            }
            return 0;
    }

    /* unknown key */
    return 1;
}

static int test_fs_features(int *f)
{
#define TESTFILE1 ".__fs2go_test_1__"
    char *p;
    VERBOSE("testing remote fs features\n");

    /* create test file */
    p = remote_path(TESTFILE1);

    if (mknod(p, S_IFREG | S_IRUSR | S_IWUSR, 0))
    {
        perror("failed to create feature test file");
        return -1;
    }

    /* test if timestamps support nanosecond presicion */
#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
    struct timespec mtime;
    struct stat st;

    /* set mtime of file to 0, 1337 */
    mtime.tv_sec = 0;
    mtime.tv_nsec = 1337;
    set_mtime(p, mtime);

    /* get stat */
    if (stat(p, &st))
    {
        perror("failed to stat feature test file");
        return -1;
    }

    /* check if nanoseconds were actually being set */
    if (st.st_mtim.tv_nsec == mtime.tv_nsec)
        *f |= FEAT_NS;
#endif

#if HAVE_SETXATTR
    /* test if extended attributes are supported */
    if (lsetxattr(p, "user.fs2go_test", "1", 1, 0) == 0 || errno != ENOTSUP)
        *f |= FEAT_XATTR;
#endif

    /* test if hard links are supported
       we currently don't support them ourselves :7
     *f |= FEAT_HARDLINKS;
     */

    /* remove test file */
    unlink(p);

    free(p);

    return 0;
#undef TESTFILE1
}

/* signal handler (prototyp inside main()) */
void sig_handler(int signo)
{
    switch (signo) {
        /* sighup blocks the working thread for 10 seconds. this gives the
           user the opportunity to unmount the remote fs */ 
        case SIGHUP:
            VERBOSE("received SIGHUP, blocking worker for 10 seconds\n");
            worker_block();
            sleep(10);
            worker_unblock();
            break;
    }
}

/* operations struct which will be passed to fuse_main() */

#ifdef DEBUG_FS_OPS
#define OPER(n) .n = debug_op_ ## n
#else
#define OPER(n) .n = op_ ## n
#endif
static struct fuse_operations fs2go_oper =
{
    OPER(init),
    OPER(destroy),
    OPER(getattr),
    OPER(fgetattr),
    OPER(access),
    OPER(readlink),
    OPER(opendir),
    OPER(readdir),
    OPER(mknod),
    OPER(mkdir),
    OPER(rmdir),
    OPER(unlink),
    OPER(link),
    OPER(symlink),
    OPER(rename),
    OPER(releasedir),
    OPER(open),
    OPER(create),
    OPER(flush),
    OPER(release),
    OPER(fsync),
    OPER(fsyncdir),
    OPER(read),
    OPER(write),
    OPER(truncate),
    OPER(chown),
    OPER(chmod),
    OPER(utimens),
    OPER(statfs),
#if HAVE_SETXATTR
    OPER(setxattr),
    OPER(getxattr),
    OPER(listxattr),
#endif
};

int main(int argc, char **argv)
{
    /* return value of fuse_main() */
    int ret;

    /* for signal handling */
    void sig_handler(int signo);
    struct sigaction sig;

    /* argument handling */
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* file name for database */
    char *db_file;


    /*------------------------*/
    /* install signal handler */
    /*------------------------*/
    
    /* set handling function */
    sig.sa_handler = sig_handler;

    /* set (no) flags */
    sig.sa_flags = 0; 

    /* empty signal mask */
    sigemptyset(&sig.sa_mask);
    
    /* add signals to catch */
    sigaddset(&sig.sa_mask, SIGHUP);

    /* finally install signal handler */
    sigaction(SIGHUP, &sig, NULL);


    /*------------------*/
    /* handle arguments */
    /*------------------*/

    if (fuse_opt_parse(&args, &fs2go_options, fs2go_opts, fs2go_opt_proc) == -1)
        return EXIT_FAILURE;

    /* after option parsing, remote mount point must be set */
    if (!REMOTE_ROOT)
    {
        fprintf(stderr, "no remote filesystem given\n");
        return EXIT_FAILURE;
    }

    /* a mount point for fs2go must also be set */
    if (!fs2go_options.fs2go_mp)
    {
        fprintf(stderr, "no mount point given\n");
        return EXIT_FAILURE;
    }


    /*---------------*/
    /* set UID / GID */
    /*---------------*/

    /* set GID first since permissions might not be
       sufficient if UID was set beforehand */
    if (fs2go_options.gid)
    {
        VERBOSE("setting gid to %d\n", fs2go_options.gid);
        if (setgid(fs2go_options.gid))
        {
            perror("setting gid");
            return EXIT_FAILURE;
        }
    }
    if (fs2go_options.uid)
    {
        VERBOSE("setting uid to %d\n", fs2go_options.uid);
        if (setuid(fs2go_options.uid))
        {
            perror("setting uid");
            return EXIT_FAILURE;
        }
    }


    /*--------------------*/
    /* initialize logging */
    /*--------------------*/

    /* if -d is specified, override logging settings */
    if (fs2go_options.debug)
        log_init(LOG_DEBUG, NULL);
    else
        log_init(fs2go_options.loglevel, fs2go_options.logfile);



    /*=========================*/
    /* INITIALIZE CACHE AND DB */
    /*=========================*/

    /* compute data root if not passed as option */
    if (!fs2go_options.data_root)
        fs2go_options.data_root = paths_data_root(REMOTE_ROOT);

    if (!is_dir(fs2go_options.data_root))
    {
        if (mkdir_rec(fs2go_options.data_root))
            FATAL("failed to create data directory %s\n", fs2go_options.data_root);
    }


    /*----------------------*/
    /* initialize cache dir */
    /*----------------------*/

    /* set cache dir */
    CACHE_ROOT = join_path(fs2go_options.data_root, "cache");

    /* store length of cache root (to save a few hundred strlen() calls)  */
    CACHE_ROOT_LEN = strlen(CACHE_ROOT);

    /* delete cache if "clear" specified */
    if (fs2go_options.clear)
    {
        VERBOSE("deleting cache\n");
        rmdir_rec(CACHE_ROOT);
    }

    /* create cache root if needed */
    if (!is_dir(CACHE_ROOT))
    {
        if (mkdir(CACHE_ROOT, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
            FATAL("failed to create cache directory %s\n", CACHE_ROOT);
    }


    /*---------------------*/
    /* initialize database */
    /*---------------------*/

    /* set db filename */
    db_file = join_path(fs2go_options.data_root, "db.sqlite");

    /* initialize tables etc */
    db_init(db_file, fs2go_options.clear);

    /* try to load filesystem features from DB */
    if (db_cfg_get_int(CFG_FS_FEATURES, &fs2go_options.fs_features))
    {
        
        /* if loading failed, try to determine them */
        if (is_mounted(REMOTE_ROOT) && is_reachable(fs2go_options.host))
        {
            if (test_fs_features(&fs2go_options.fs_features))
            {
                ERROR("failed to test remote fs features\n");
                fs2go_options.fs_features = 0;
            }
            /* test succeeded, store value for next time */
            else
                db_cfg_set_int(CFG_FS_FEATURES, fs2go_options.fs_features);
        }
        /* nag and assume that no features available (but don't save that) */
        else
        {
            ERROR("could not determine remote fs features");
            fs2go_options.fs_features = 0;
        }
    }


    /*---------------------------*/
    /* initialize sync/job stuff */
    /*---------------------------*/
    if (sync_init())
        FATAL("error initializing sync");
    if (job_init())
        FATAL("error initializing job queue");


    /*----------------------*/
    /* print options to log */
    /*----------------------*/
    log_options(LOG_VERBOSE, fs2go_options);


    /*-----------------*/
    /* run fuse_main() */
    /*-----------------*/
    ret = fuse_main(args.argc, args.argv, &fs2go_oper, NULL);


    /*------*/
    /* exit */
    /*------*/

    /* store and free job data */
    job_destroy();

    /* store and free sync data */
    sync_destroy();

    /* free arguments */
    fuse_opt_free_args(&args);

    /* close database connection */
    db_destroy();


    /* end logging */
    INFO("exiting\n");
    log_destroy();


    /* return fuse_main()s return value */
    return ret;
}
