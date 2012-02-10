#include <features.h>
#include "config.h"

#include "fs2go.h"

#include "log.h"
#include "funcs.h"
#include "sync.h"
#include "job.h"
#include "worker.h"
#include "fuseops.h"
#include "db.h"
#include "paths.h"

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

static int fs2go_debug = 0;
static int fs2go_state = STATE_OFFLINE;
static pthread_mutex_t m_fs2go_state = PTHREAD_MUTEX_INITIALIZER;

struct options fs2go_options = OPTIONS_INIT;
fs_feat_t fs_features = 0;

int get_state() {
	int s;
	pthread_mutex_lock(&m_fs2go_state);
	s = fs2go_state;
	pthread_mutex_unlock(&m_fs2go_state);
	return s;
}

void set_state(int s, int *oldstate) {
	pthread_mutex_lock(&m_fs2go_state);
	if (s == STATE_ONLINE && s != fs2go_state)
		VERBOSE("going ONLINE\n");
	else if (s == STATE_OFFLINE && s != fs2go_state)
		VERBOSE("going OFFLINE\n");
	else if (s == STATE_EXITING)
		VERBOSE("exiting\n");

	if (oldstate)
		*oldstate = fs2go_state;

	/* don't change status when we're supposed to exit */
	if (fs2go_state != STATE_EXITING)
		fs2go_state = s;
	pthread_mutex_unlock(&m_fs2go_state);
}

static void print_usage() {
	char *s = "usage: " PROG_NAME " remote_fs mountpoint [options]\n"
	"\n"
	"general options:\n"
	/*"  -o opt,[opt...]	mount options\n"*/
	" -h --help			display help\n"
	" -v --version			display version\n"
	" -d --debug			enable debugging output, don't fork to background\n"
	" -f --foreground		don't fork to background\n"
	"\n"
	PROG_NAME " options:\n"
	" -r --remote-host <host>	hostname or IP address to PING for remote fs availability\n"
	" -p --pid <filename>		file containing PID to test for remote fs avialability\n"
	"    --scan <seconds>		interval to wait before scanning remote fs for changes. default is " STR(DEF_SCAN_INTERVAL) "\n"
	" -c --conflict			conflict resolution mode. possible values:\n"
	"				newer, mine, theirs. default is 'newer'\n"
	"    --bprefix\n"
	"    --bsuffix			backup prefix/suffix (see the manual for more information)\n"
	"    --clear			delete database and cache\n"
	"    --loglevel <level>		logging level, possible values: none, error, info, verbose, fsop, debug\n"
	"				each including its predecessors. default is 'none'\n"
	"    --logfile <file>		obvious, isn't it? default ist stderr\n"
	"\n"
	"filesystem specific options:\n"
	"    --no-mode			don't sync access permissions\n"
	"    --no-owner			don't sync user-ownership\n"
	"    --no-group			don't sync group-ownership\n"
	#if HAVE_SETXATTR
	"    --no-xattr			don't sync extended attributes (set with getfattr)\n"
	"    --sshfs			same as \"--no-owner --no-group --no-xattr\"\n"
	#else
	"    --sshfs			same as \"--no-owner --no-group\"\n"
	#endif
	"    --nfs			same as \"--no-owner --no-group\"\n"
	"";

	/*"  -b --backup-dir <dir>" */

	fprintf(stderr, s);
}

static void print_version() {
	printf("%s Version %s\n", PROG_NAME, PROG_VERSION);
}

static void log_options(int loglevel, struct options opt) {
	const char *tmp;
	#define YESNO(x) (x) ? "yes" : "no"
	log_print(loglevel, "fs2go options:\n");
	log_print(loglevel, "mount point: %s\n", opt.fs2go_mp);
	log_print(loglevel, "remote fs: %s\n", opt.remote_root);
	log_print(loglevel, "cache root: %s\n", opt.cache_root);
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
	log_print(loglevel, "nanosecond timestamps: %s\n", YESNO((fs_features & FEAT_NS)));
	#if HAVE_SETXATTR
	log_print(loglevel, "extended attributes: %s\n", YESNO((fs_features & FEAT_XATTR)));
	#endif
}

/** macro to define options */
#define OPT_KEY(t, p, v) { t, offsetof(struct options, p), v }
static struct fuse_opt fs2go_opts[] = {
	FUSE_OPT_KEY("--uid %s", FS2GO_OPT_UID),
	FUSE_OPT_KEY("uid=%s", FS2GO_OPT_UID),
	FUSE_OPT_KEY("--gid %s", FS2GO_OPT_GID),
	FUSE_OPT_KEY("gid=%s", FS2GO_OPT_GID),

	OPT_KEY("--db %s", db_file, 0),
	OPT_KEY("db=%s", db_file, 0),
	OPT_KEY("--cache %s", cache_root, 0),
	OPT_KEY("cache=%s", cache_root, 0),

	OPT_KEY("-r %s", host, 0),
	OPT_KEY("--remote-host %s", host, 0),
	OPT_KEY("remote-host=%s", host, 0),
	OPT_KEY("-p %s", pid_file, 0),
	OPT_KEY("--pid %s", pid_file, 0),
	OPT_KEY("pid=%s", pid_file, 0),

	OPT_KEY("--scan %u", scan_interval, 0),
	OPT_KEY("scan=%u", scan_interval, 0),

	FUSE_OPT_KEY("-c %s", FS2GO_OPT_CONFLICT),
	FUSE_OPT_KEY("--conflict %s", FS2GO_OPT_CONFLICT),
	FUSE_OPT_KEY("conflict=%s", FS2GO_OPT_CONFLICT),

	OPT_KEY("--bprefix %s", backup_prefix, 0),
	OPT_KEY("bprefix=%s", backup_prefix, 0),
	OPT_KEY("--bsuffix %s", backup_suffix, 0),
	OPT_KEY("bsuffix=%s", backup_suffix, 0),

	OPT_KEY("--clear", clear, 1),
	OPT_KEY("clear", clear, 1),

	FUSE_OPT_KEY("--loglevel %s", FS2GO_OPT_LOGLEVEL),
	FUSE_OPT_KEY("loglevel=%s", FS2GO_OPT_LOGLEVEL),
	OPT_KEY("--logfile %s", logfile, 0),
	OPT_KEY("logfile=%s", logfile, 0),

	FUSE_OPT_KEY("--no-mode", FS2GO_OPT_NO_MODE),
	FUSE_OPT_KEY("no-mode", FS2GO_OPT_NO_MODE),
	FUSE_OPT_KEY("--no-owner", FS2GO_OPT_NO_OWNER),
	FUSE_OPT_KEY("no-owner", FS2GO_OPT_NO_OWNER),
	FUSE_OPT_KEY("--no-group", FS2GO_OPT_NO_GROUP),
	FUSE_OPT_KEY("no-group", FS2GO_OPT_NO_GROUP),
	#if HAVE_SETXATTR
	FUSE_OPT_KEY("--no-xattr", FS2GO_OPT_NO_XATTR),
	FUSE_OPT_KEY("no-xattr", FS2GO_OPT_NO_XATTR),
	#endif
	FUSE_OPT_KEY("--sshfs", FS2GO_OPT_SSHFS),
	FUSE_OPT_KEY("sshfs", FS2GO_OPT_SSHFS),
	FUSE_OPT_KEY("--nfs", FS2GO_OPT_NFS),
	FUSE_OPT_KEY("nfs", FS2GO_OPT_NFS),

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

static int fs2go_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	const char *val = NULL;
	char *p, *p2;
	char *endptr;
	struct passwd *pw;
	struct group *gr;

	switch (key) {
		case FUSE_OPT_KEY_NONOPT:
			/* first non-opt keyword is remote fs */
			if (!fs2go_options.remote_root) {
				/* transform to absolute path */
				if (*arg != '/') {
					if ((p = malloc(PATH_MAX * sizeof(char))) == NULL)
						FATAL("memory allocation failed\n");
					getcwd(p, PATH_MAX);
					strcat(p, "/");
					strcat(p, arg);
				}
				else {
					if ((p = strdup(arg)) == NULL)
						FATAL("memory allocation failed\n");
				}

				/* cut off trailing slash */
				if (p[strlen(p)-1] == '/')
					p[strlen(p)-1] = '\0';

				if (!is_dir(p)) {
					fprintf(stderr, "remote mount point \"%s\" is not a directory\n", p);
					exit(EXIT_FAILURE);
				}
				if ((p2 = malloc((strlen("-ofsname=") + strlen(p) + 1) * sizeof(char)))) {
					strcpy(p2, "-ofsname=");
					strcat(p2, p);
					fuse_opt_add_arg(outargs, p2);
				}

				REMOTE_ROOT_LEN = strlen(p);
				if ((REMOTE_ROOT = malloc((REMOTE_ROOT_LEN+1)*sizeof(char))) == NULL)
					FATAL("memory allocation failed\n");
				memcpy(REMOTE_ROOT, p, REMOTE_ROOT_LEN+1);
				free(p);

				return 0;
			}
			/* second one is "our" mount point */
			else if (!fs2go_options.fs2go_mp) {
				fs2go_options.fs2go_mp = strdup(arg);
				fuse_opt_add_arg(outargs, arg);
				return 0;
			}
			return 1;

		/* --version and --help */
		case FS2GO_OPT_VERSION:
			print_version();
			exit(EXIT_SUCCESS);

		case FUSE_OPT_KEY_OPT:
		case FS2GO_OPT_HELP:
			print_usage();
			exit(EXIT_SUCCESS);

		/* --debug and --foreground */
		case FS2GO_OPT_DEBUG:
			fprintf(stderr, "INFO: -d overwrites --loglevel and --logfile\n");
			fs2go_debug = 1;
			fuse_opt_add_arg(outargs, "-d");
			return 0;

		case FS2GO_OPT_FOREGROUND:
			fuse_opt_add_arg(outargs, "-f");
			return 0;

		case FS2GO_OPT_UID:
			val = arg + strlen((*arg == '-') ? "--uid" : "uid=");
			fs2go_options.uid = (uid_t)strtol(val, &endptr, 10);
			pw = (*endptr != '\0') ? getpwnam(val) : getpwuid(fs2go_options.uid);
			if (!pw)
					FATAL("could not find user \"%s\"\n", val);
			fs2go_options.uid = pw->pw_uid;
			if (!fs2go_options.gid)
				fs2go_options.gid = pw->pw_gid;
			return 0;

		case FS2GO_OPT_GID:
			val = arg + strlen((*arg == '-') ? "--gid" : "gid=");
			fs2go_options.gid = (gid_t)strtol(val, &endptr, 10);
			if (*endptr != '\0') {
				if (!(gr = getgrnam(val)))
					FATAL("could not find group \"%s\"\n", val);
				fs2go_options.uid = gr->gr_gid;
			}
			return 0;

		/* --no-mode, --no-owner etc. */
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

		/* --loglevel */
		case FS2GO_OPT_LOGLEVEL:
			if (*arg == '-')
				val = arg + strlen("--loglevel");
			else
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
			else {
				print_usage();
				exit(EXIT_FAILURE);
			}
			return 0;

		/* --conflict */
		case FS2GO_OPT_CONFLICT:
			if (*arg == '-') {
				if (arg[1] == '-')
					val = arg + strlen("--conflict");
				else
					val = arg + strlen("-c");
			}
			else
				val = arg + strlen("conflict=");

			if (!strcmp(val, "newer") || !strcmp(val, "n"))
				fs2go_options.conflict = CONFLICT_NEWER;
			else if (!strcmp(val, "theirs") || !strcmp(val, "t"))
				fs2go_options.conflict = CONFLICT_THEIRS;
			else if (!strcmp(val, "mine") || !strcmp(val, "m"))
				fs2go_options.conflict = CONFLICT_MINE;
			else {
				print_usage();
				exit(EXIT_FAILURE);
			}
			return 0;
	}

	return 1;
}

static int test_fs_features(fs_feat_t *f) {
#define TESTF1 ".__fs2go_test_1__"
	char *p;
	VERBOSE("testing remote fs features\n");

	p = remote_path(TESTF1, strlen(TESTF1));
	if (mknod(p, S_IFREG | S_IRUSR | S_IWUSR, 0)) {
		perror("failed to create feature test file");
		return -1;
	}

	/* test if timestamps support nanosecond presicion */
	#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
	struct timespec mtime;
	struct stat st;

	mtime.tv_sec = 0;
	mtime.tv_nsec = 1337;
	set_mtime(p, mtime);
	if (stat(p, &st)) {
		perror("failed to stat feature test file");
		return -1;
	}

	if (st.st_mtim.tv_nsec == mtime.tv_nsec)
		*f |= FEAT_NS;
	#endif

	/* test if extended attributes are supported */
	#if HAVE_SETXATTR
	if (lsetxattr(p, "user.fs2go_test", "1", 1, 0) == 0 || errno != ENOTSUP)
		*f |= FEAT_XATTR;
	#endif

	/* test if hard links are supported
	we currently don't support them ourselves :7
		*f |= FEAT_HARDLINKS;
	*/

	unlink(p);
	free(p);

	return 0;
#undef TESTF1
}

void sig_handler(int signo) {
	switch (signo) {
		case SIGHUP:
			VERBOSE("received SIGHUP, blocking worker for 10 seconds\n");
			worker_block();
			sleep(10);
			worker_unblock();
			break;
	}
}

#define OPER(n) .n = op_ ## n
static struct fuse_operations fs2go_oper = {
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

int main(int argc, char **argv) {
	int ret = 0;
	void sig_handler(int signo);
	struct sigaction sig;

	sig.sa_handler = sig_handler;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;

	sigaction(SIGHUP, &sig, NULL);

	/* parse opts, return 1 on error */
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (fuse_opt_parse(&args, &fs2go_options, fs2go_opts, fs2go_opt_proc) == -1)
		return EXIT_FAILURE;

	if (!REMOTE_ROOT) {
		fprintf(stderr, "remote filesystem is mandatory\n");
		return EXIT_FAILURE;
	}

	/* switch uid / gid */
	if (fs2go_options.uid) {
		if (setuid(fs2go_options.uid)) {
			perror("setting uid");
			return EXIT_FAILURE;
		}
	}
	if (fs2go_options.gid) {
		if (setgid(fs2go_options.gid)) {
			perror("setting gid");
			return EXIT_FAILURE;
		}
	}

	/* if -d is specified, override logging settings */
	if (fs2go_debug)
		log_init(LOG_DEBUG, NULL);
	else
		log_init(fs2go_options.loglevel, fs2go_options.logfile);

	/* set cache dir (if not specified) */
	if (!CACHE_ROOT) {
		CACHE_ROOT = get_cache_root(REMOTE_ROOT);
	}
	else if (!is_dir(CACHE_ROOT)) {
		FATAL("specified cache dir \"%s\" is not a directory\n", CACHE_ROOT);
	}
	CACHE_ROOT_LEN = strlen(CACHE_ROOT);

	/* delete cache if --clear specified */
	if (fs2go_options.clear) {
		struct stat st;
		VERBOSE("deleting cache\n");
		if (lstat(CACHE_ROOT, &st) == 0)
			rmdir_rec(CACHE_ROOT);
	}
	/* create cache root */
	if (mkdir_rec(CACHE_ROOT))
		FATAL("failed to create cache directory %s\n", CACHE_ROOT);

	/* create (if needed) directory where the database will be stored */
	if (!fs2go_options.db_file)
		fs2go_options.db_file = get_db_fn(REMOTE_ROOT);

	if (!is_reg(fs2go_options.db_file)) {
		char *dbdir;
		dbdir = dirname_r(fs2go_options.db_file);
		if (!dbdir) {
			FATAL("failed to allocate memory\n");
		}

		if (mkdir_rec(dbdir))
			FATAL("could not create directory \"%s\" for db\n", dbdir);
		free(dbdir);
	}

	/* initialize tables etc */
	db_init(fs2go_options.db_file, fs2go_options.clear);

	/* load filesystem features from DB, else run tests or complain*/
	if (db_cfg_get_int(CFG_FS_FEATURES, &fs_features)) {
		if (is_mounted(REMOTE_ROOT) && is_reachable(fs2go_options.host)) {
			if (test_fs_features(&fs_features)) {
				ERROR("failed to test remote fs features\n");
				fs_features = 0;
			}
			else
				db_cfg_set_int(CFG_FS_FEATURES, fs_features);
		}
		else {
			ERROR("could not determine remote fs features");
			fs_features = 0;
		}
	}
	log_options(LOG_VERBOSE, fs2go_options);

	/* start */
	ret = fuse_main(args.argc, args.argv, &fs2go_oper, NULL);

	VERBOSE("storing jobs\n");
	job_store_queue();
	VERBOSE("storing sync data\n");
	sync_store();
	sync_ht_free();

	/* clean up */
	fuse_opt_free_args(&args);

	db_destroy();

	INFO("exiting\n");
	log_destroy();
	return ret;
}
