/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_TRANSFER_H
#define DISCOFS_TRANSFER_H

#include "job.h"

#define TRANSFER_FAIL -1
#define TRANSFER_OK 0
#define TRANSFER_FINISH 1

int transfer(const char *from, const char *to);
int transfer_begin(struct job *j);

void transfer_rename_dir(const char *from, const char *to);
void transfer_rename(const char *to);

void transfer_abort(void);

int transfer_instant_pull(const char *path);

#endif
