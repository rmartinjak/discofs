/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef FS2GO_PATHS_H
#define FS2GO_PATHS_H
#include "config.h"

char *get_cache_root(const char *remote, const char *data_root);
char *get_db_fn(const char *remote, const char *data_root);

#endif
