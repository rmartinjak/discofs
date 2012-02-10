#include "config.h"
#include "fuseops.h"

#include "fs2go.h"
#include "log.h"
#include "funcs.h"
#include "sync.h"
#include "job.h"
#include "db.h"
#include "lock.h"
#include "worker.h"
#include "transfer.h"
#include "bst.h"

#include <fuse.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#if HAVE_SETXATTR
#include <attr/xattr.h>
#endif

static pthread_t t_worker, t_state;

/** called when fs is initialized.
starts worker and state checking thread
*/
void *op_init(struct fuse_conn_info *conn) {
	VERBOSE("restoring sync data\n");
	TIMEDCALL(sync_load());

	VERBOSE("starting state check thread\n");
	if (pthread_create(&t_state, NULL, worker_statecheck, NULL))
		FATAL("failed to create thread\n");

	VERBOSE("starting worker thread\n");
	if (pthread_create(&t_worker, NULL, worker_main, NULL))
		FATAL("failed to create thread\n");

	return NULL;
}

void op_destroy(void *p) {
	set_state(STATE_EXITING, NULL);

	DEBUG("joining state check thread\n");
	pthread_join(t_state, NULL);

	DEBUG("joining worker thread\n");
	pthread_join(t_worker, NULL);
}

int op_getattr(const char *path, struct stat *buf) {
	FSOP("getattr(%s)\n", path);
	int res;
	int err;
	char *p;
	size_t p_len = strlen(path);

	p = cache_path(path, p_len);

	res = lstat(p, buf);
	err = errno;
	free(p);

	if (res == -1 && ONLINE) {
		p = remote_path(path, p_len);
		res = lstat(p, buf);
		free(p);
	}

	if (res == -1)
		return -err;
	return 0;
}

int op_fgetattr(const char *path, struct stat *buf, struct fuse_file_info *fi) {
	FSOP("fgetattr(%s)\n", path);
	int res;

	res = fstat(FH_FD(fi->fh), buf);

	if (res == -1)
		return -errno;
	return 0;
}

int op_access(const char *path, int mode) {
	FSOP("access(%s)\n", path);
	int res;
	char *p, *pc, *pr;
	size_t p_len = strlen(path);

	pc = cache_path(path, p_len);
	pr = remote_path(path, p_len);

	if (ONLINE && !has_lock(path, LOCK_OPEN) && strcmp(path, "/") != 0) {
		p = pr;

		/* doesn't exit remote: */
		if (get_sync(path) == SYNC_NOT_FOUND) {
			if (has_lock(path, LOCK_TRANSFER) || has_job(path, JOB_PUSH))
				p = pc;
			else {
				free(pc);
				free(pr);
				return -ENOENT;
			}
		}
	}
	else {
		p = pc;
	}

	res = access(p, mode);

	free(pc);
	free(pr);

	if (res == -1)
		return -errno;
	return 0;
}

int op_readlink(const char *path, char *buf, size_t bufsize) {
	FSOP("readlink(%s)\n", path);
	int res;
	char *p;

	p = get_path(path, strlen(path));
	res = readlink(p, buf, bufsize);
	free(p);

	if (res == -1)
		return -errno;
	return 0;
}

int op_opendir(const char *path, struct fuse_file_info *fi) {
	FSOP("opendir(%s)\n", path);
	char *p, *p2;
	DIR **dirp;
	DIR **d;
	size_t p_len = strlen(path);

	dirp = malloc(2 * sizeof(DIR *));
	if (!dirp)
		return -EIO;

	worker_block();
	d = dirp;

	/* open cache dir */
	p = cache_path(path, p_len);
	if ((*d = opendir(p)) == NULL) {
		if (errno == ENOENT && ONLINE) {
			p2 = remote_path(path, p_len);
			clone_dir(p2, p);
			free(p2);
			*d = opendir(p);
		}
		else {
			free(p);
			return -errno;
		}
	}
	free(p);

	d++;
	if (ONLINE) {
		p = remote_path(path, p_len);
		*d = opendir(p);
		free(p);
	}
	else {
		*d = NULL;
	}

	fi->fh = (uint64_t)dirp;
	return 0;
}

int op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	FSOP("readdir(%s)\n", path);
	int res;
	DIR **dirp;
	struct bst tree = BST_INIT;

	size_t dbufsize;
	struct dirent *ent;
	bstdata_t hash;
	struct dirent *dbuf;
	struct stat st;
	memset(&st, 0, sizeof(st));


	dirp = (DIR **)fi->fh;
	if (!*dirp)
		return -EBADF;

	dbufsize = dirent_buf_size(*dirp);
	dbuf = malloc(dbufsize);

	res = readdir_r(*dirp, dbuf, &ent);
	if (res > 0)
		return -res;
	if (!ent && ONLINE)
		return -errno;

	int n = 2;
	while (n-- && *dirp) {
		do {
			if (!ent)
				continue;

			/* hide partfile */
			if (is_partfile(ent->d_name))
				continue;

			hash = (bstdata_t)djb2(ent->d_name, -1);

			if (bst_contains(&tree, hash))
				continue;

			memset(&st, 0, sizeof(st));
			st.st_ino = ent->d_ino;
			st.st_mode = DTTOIF(ent->d_type);
			if (filler(buf, ent->d_name, &st, 0)) {
				bst_clear(&tree);
				free(dbuf);
				return -ENOMEM;
			}
			bst_insert(&tree, hash);
		} while ((res = readdir_r(*dirp, dbuf, &ent)) == 0 && ent);

		dirp++;
	}

	bst_clear(&tree);
	free(dbuf);
	return 0;
}

int op_mknod(const char *path, mode_t mode, dev_t rdev) {
	FSOP("mknod(%s)\n", path);
	int res;
	size_t p_len = strlen(path);

	char *p;

	p = cache_path(path, p_len);
	res = mknod(p, mode, rdev);
	if (res != 0)
		return -errno;
	free(p);

	if (ONLINE) {
		p = remote_path(path, p_len);
		res = mknod(p, mode, rdev);
		free(p);
		if (res != 0)
			return -errno;
		set_sync(path);
	}
	else {
		schedule_push(path);
	}

	return res;
}

int op_mkdir(const char *path, mode_t mode) {
	FSOP("mkdir(%s)\n", path);
	return job(JOB_MKDIR, path, mode, 0, NULL, NULL);
}

int op_rmdir(const char *path) {
	FSOP("rmdir(%s)\n", path);
	return job(JOB_RMDIR, path, 0, 0, NULL, NULL);
}
int op_unlink(const char *path) {
	FSOP("unlink(%s)\n", path);
	return job(JOB_UNLINK, path, 0, 0, NULL, NULL);
}

int op_symlink(const char *to, const char *from) {
	FSOP("symlink(%s, %s)\n", to, from);
	int res;
	res = job(JOB_SYMLINK, from, 0, 0, to, NULL);
	return res;
}

int op_link(const char *from, const char *to) {
	FSOP("link(%s, %s)\n", from, to);
	return -ENOTSUP;
}

int op_rename(const char *from, const char *to) {
	FSOP("rename(%s, %s)\n", from, to);
	return job(JOB_RENAME, from, 0, 0, to, NULL);
}

int op_releasedir(const char* path, struct fuse_file_info *fi) {
	FSOP("releasedir(%s)\n", path);
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
static int op_open_create(int op, const char *path, mode_t mode, struct fuse_file_info *fi) {
	FSOP("open_create(%s)\n", path);
	int sync;
	int *fh;
	char *pc, *pr;

	if ((fh = malloc(FH_SIZE)) == NULL)
		return -EIO;	
	
	FH_FLAGS(fh) = 0;

	if (ONLINE && !has_lock(path, LOCK_OPEN)) {
		sync = get_sync(path);

		if (sync == -1) {
			free(fh);
			return -EIO;
		}

		if (has_job(path, JOB_PULL)) {
			pthread_mutex_lock(&m_instant_pull);
			pthread_mutex_unlock(&m_instant_pull);
			db_delete_jobs(path, JOB_PULL);

			if (has_lock(path, LOCK_TRANSFER))
				transfer_abort();

			instant_pull(path);
		}
		else if (sync == SYNC_NEW || sync == SYNC_MOD) {
			/* wait until eventually running instant_pull is finished */
			pthread_mutex_lock(&m_instant_pull);
			pthread_mutex_unlock(&m_instant_pull);

			/* maybe the last instant_pull pulled exactly the file we
			want to open. if not, instant_pull it now */
			sync = get_sync(path);
			if (sync == SYNC_NEW || sync == SYNC_MOD)
				instant_pull(path);
		}
		else if (sync == SYNC_CHG) {
			pc = cache_path(path, strlen(path));
			pr = remote_path(path, strlen(path));
			copy_attrs(pr, pc);
			free(pr);
			free(pc);
		}
	}

	set_lock(path, LOCK_OPEN);

	pc = cache_path(path, strlen(path));
	if (op == OP_OPEN)
		FH_FD(fh) = open(pc, fi->flags);
	else
		FH_FD(fh) = open(pc, O_WRONLY|O_CREAT|O_TRUNC, mode);
	free(pc);

	/* omg! */
	if (FH_FD(fh) == -1) {
		free(fh);
		return -errno;
	}

	fi->fh = (uint64_t)fh;
	return 0;
}

int op_open(const char *path, struct fuse_file_info *fi) {
	FSOP("open(%s)\n", path);
	return op_open_create(OP_OPEN, path, 0, fi);
}

int op_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	FSOP("create(%s)\n", path);
	return op_open_create(OP_CREATE, path, mode, fi);
}

int op_flush(const char *path, struct fuse_file_info *fi) {
	FSOP("flush(%s)\n", path);
	int res;
	/* forward the flush op to underlying fd */
	res = close(dup(FH_FD(fi->fh)));
	if (res == -1)
		return -errno;

	return 0;
}

int op_release(const char *path, struct fuse_file_info *fi) {
	int res;
	FSOP("release(%s)\n", path);
	remove_lock(path, LOCK_OPEN);
	res = close(FH_FD(fi->fh));

	/* file written -> schedule push */
	if (FH_FLAGS(fi->fh) & FH_WRITTEN) {
		schedule_push(path);
	}
	free((int*)fi->fh);
	return res;
}

int op_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
	FSOP("fsync(%s)\n", path);
	int res;
	if (isdatasync)
		res = fdatasync(FH_FD(fi->fh));
	else
		res = fsync(FH_FD(fi->fh));
	if (res == -1)
		return -errno;
	return 0;
}

int op_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi) {
	FSOP("fsyncdir(%s)\n", path);
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

int op_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	FSOP("read(%s)\n", path);
	int res;
	res = pread(FH_FD(fi->fh), (void *)buf, size, offset);
	if (res == -1)
		return -errno;
	return res;
}

int op_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	FSOP("write(%s)\n", path);
	int res;
	res = pwrite(FH_FD(fi->fh), (void *)buf, size, offset);
	if (res == -1)
		return -errno;

	FH_FLAGS(fi->fh) |= FH_WRITTEN;
	return res;
}

int op_truncate(const char *path, off_t size) {
	FSOP("truncate(%s)\n", path);
	int res;
	char *p;
	size_t p_len = strlen(path);

	p = cache_path(path, p_len);
	res = truncate(p, size);
	free(p);
	if (res == -1)
		return -errno;

	if (ONLINE) {
		if (!has_lock(path, LOCK_OPEN)) {
			if (has_lock(path, LOCK_TRANSFER)) {
				transfer_abort();
				remove_lock(path, LOCK_TRANSFER);
			}

			p = remote_path(path, p_len);
			res = truncate(p, size);
			free(p);
			if (res == -1 && !has_job(path, JOB_PUSH))
				return -errno;
		}
	}
	else {
		schedule_push(path);
	}

	return 0;
}

int op_chown(const char *path, uid_t uid, gid_t gid) {
	FSOP("chown(%s)\n", path);
	return job(JOB_CHOWN, path, (jobp_t)uid, (jobp_t)gid, NULL, NULL);
}

int op_chmod(const char *path, mode_t mode) {
	FSOP("chmod(%s)\n", path);
	return job(JOB_CHMOD, path, (jobp_t)mode, 0, NULL, NULL);
}

int op_utimens(const char *path, const struct timespec ts[2]) {
	FSOP("utimens(%s)\n", path);
	int res;
	char *p;
	size_t p_len = strlen(path);

	p = cache_path(path, p_len);
	res = utimensat(-1, p, ts, AT_SYMLINK_NOFOLLOW);
	free(p);
	if (res == -1)
		return -errno;

	if (ONLINE) {
		p = remote_path(path, p_len);
		utimensat(-1, p, ts, AT_SYMLINK_NOFOLLOW);
		free(p);
	}

	return 0;
}

int op_statfs(const char *path, struct statvfs *buf) {
	FSOP("statfs(%s)\n", path);
	int res;
	char *p;

	p = get_path(path, strlen(path));
	res = statvfs(p, buf);
	free(p);

	if (res == -1)
		return -errno;
	return 0;
}

#if HAVE_SETXATTR
int op_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
	FSOP("setxattr(%s)\n", path);

	if (!(fs_features & FEAT_XATTR))
		return -ENOTSUP;

	return job(JOB_SETXATTR, path, (jobp_t)size, (jobp_t)flags, name, value);
}

int op_getxattr(const char *path, const char *name, char *value, size_t size) {
	FSOP("getxattr(%s)\n", path);
	int res;
	char *p;

	if (!(fs_features & FEAT_XATTR))
		return -ENOTSUP;

	p = get_path(path, strlen(path));

	res = lgetxattr(p, name, value, size);
	free(p);

	if (res == -1)
		return -errno;
	return 0;
}

int op_listxattr(const char *path, char *list, size_t size) {
	FSOP("listxattr(%s)\n", path);
	int res;
	char *p;

	if (!(fs_features & FEAT_XATTR))
		return -ENOTSUP;

	p = get_path(path, strlen(path));
	res = llistxattr(p, list, size);
	free(p);

	if (res == -1)
		return -errno;
	return 0;
}
#endif
