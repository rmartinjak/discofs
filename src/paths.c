/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "paths.h"

#include "funcs.h"

#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <unistd.h>
#include <pwd.h>

static char *get_data_root(const char *remote, const char *env, const char *rep) {
	char *ret;
	uid_t uid;
	struct passwd *pwd;

	char *tmp;
	CALLOC(tmp, 2048, sizeof(char));

	char *str = getenv(env);
	if (!str || strcmp(str, "") == 0) {
		uid = getuid();
		pwd = getpwuid(uid);
		strcat(tmp, pwd->pw_dir);
		strcat(tmp, "/");
		strcat(tmp, rep);
	}
	else
		strcpy(tmp, str);

	CALLOC(ret, strlen(tmp) + 1, sizeof(char));
	strcpy(ret, tmp);
	free(tmp);
	return ret;
}

char *get_cache_root(const char *remote) {
	char *ret;
	char *tmp;
	CALLOC(tmp, 2048, sizeof(char));

	sprintf(tmp, "%s/%s/%lu",
			get_data_root(remote, "XDG_CACHE_HOME", ".cache"),
			"fs2go",
			djb2(remote, -1)
		);
	CALLOC(ret, strlen(tmp) + 1, sizeof(char));
	strcpy(ret, tmp);
	free(tmp);
	return ret;
}

char *get_db_fn(const char *remote) {
	char *ret;
	char *tmp;
	CALLOC(tmp, 2048, sizeof(char));

	sprintf(tmp, "%s/%s/%lu.sqlite",
			get_data_root(remote, "XDG_DATA_HOME", ".local/share"),
			"fs2go",
			djb2(remote, -1)
		);
	CALLOC(ret, strlen(tmp) + 1, sizeof(char));
	strcpy(ret, tmp);
	free(tmp);
	return ret;
}
#endif
