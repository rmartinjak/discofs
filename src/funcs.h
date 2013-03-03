/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_FUNCS_H
#define DISCOFS_FUNCS_H

#include "config.h"

#include "discofs.h"
#include "state.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>

#define STRINGIZE1(x) # x
#define STR(x) STRINGIZE1(x)

#define FATAL(...) { printf("FATAL " __VA_ARGS__); exit(EXIT_FAILURE); }

#define PING_COUNT "5"
#define PING_INTERVAL "0.2"
#define PING_DEADLINE "2"


unsigned long djb2(const char *str, size_t n);

char *join_path2(const char *p1, size_t n1, const char *p2, size_t n2);
#define join_path(p1, p2) join_path2(p1, 0, p2, 0)

#define remote_path2(p, n) join_path2(REMOTE_ROOT, REMOTE_ROOT_LEN, p, n)
#define remote_path(p) remote_path2(p, 0)

#define cache_path2(p, n) join_path2(CACHE_ROOT, CACHE_ROOT_LEN, p, n)
#define cache_path(p) cache_path2(p, 0)

int is_running(const char *pidfile);
int is_mounted(const char *mpoint);
int is_reachable(const char *host);

int copy_rec(const char *from, const char *to);
int copy_symlink(const char *from, const char *to);
int copy_file(const char *from, const char *to);
int copy_attrs(const char *from, const char *to);
int clone_dir(const char *from, const char *to);

#if HAVE_SETXATTR
int copy_xattrs(const char *from, const char *to);
#endif

int is_nonexist(const char *path);
int is_reg(const char *path);
int is_lnk(const char *path);
int is_dir(const char *path);

char *affix_filename(const char *path, const char *prefix, const char *suffix);

char *basename_r(const char *path);
char *dirname_r(const char *path);

int mkdir_rec(const char *path);
int rmdir_rec(const char *path);

size_t dirent_buf_size(DIR * dirp);
#endif
