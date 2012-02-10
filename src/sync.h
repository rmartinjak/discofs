#ifndef FS2GO_SYNC_H
#define FS2GO_SYNC_H

#include "config.h"
#include "fs2go.h"

#include <sys/stat.h>
#include <sys/types.h>

#define SYNC_SYNC 0
#define SYNC_MOD 1
#define SYNC_CHG 2
#define SYNC_NEW 4
#define SYNC_NOT_FOUND 8

#if HAVE_UTIMENSAT && HAVE_CLOCK_GETTIME
typedef struct timespec sync_xtime_t;
#else
typedef time_t sync_xtime_t;
#endif

struct sync {
	char *path;
	sync_xtime_t mtime;
	sync_xtime_t ctime;
};

void sync_free(struct sync *s);
void sync_free2(struct sync *s);

int set_sync(const char *path);
int get_sync_stat(const char *path, struct stat *buf);
#define get_sync(p) get_sync_stat(p, NULL)

void sync_ht_free(void);
struct sync* sync_ht_insert(const char *path, sync_xtime_t mtime, sync_xtime_t ctime);
int sync_ht_get(const char *path, struct sync *s);

int sync_load();
int sync_store();

int sync_rename_dir(const char *from, const char *to);
int sync_delete_dir(const char *path);
int sync_rename_file(const char *from, const char *to);
int sync_delete_file(const char *path);

#endif
