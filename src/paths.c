/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "paths.h"

#include "funcs.h"

#include <stdlib.h>
#include <string.h>

static char *get_home_dir(void);

#ifdef __linux__
#include <unistd.h>
#include <pwd.h>


static char *get_home_dir(void)
{
    char *ret;
    uid_t uid;
    struct passwd *pwd;

    ret = getenv("HOME");
    if (!ret || *ret != '/') {
        uid = getuid();
        pwd = getpwuid(uid);
        ret = pwd->pw_dir;
    }

    return ret;
}

char *paths_data_root(const char *remote)
{
    char *ret;
    char *tmp;
    char *home;
    char *root;
    char *hash;

    tmp = malloc(2048);
    *tmp = '\0';
    hash = malloc(30);

    if (!tmp || !hash) {
        FATAL("memory allocation failed\n");
    }

    sprintf(hash, "%lu", djb2(remote, -1));

    /* use $XDG_DATA_HOME */
    root = getenv("XDG_DATA_HOME");
    if (!root || *root != '/') {
        /* else use ~/.local/share */
        home = get_home_dir();
        root = join_path2(home, ".local/share" );
        strcat(tmp, root);
        free(root);
    }
    else {
        strcat(tmp, root);
    }

    strcat(tmp, "/fs2go/");
    strcat(tmp, hash);

    ret = strdup(tmp);
    free(tmp);

    return ret;
}
#endif // __linux__
