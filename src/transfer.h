/*! @file
 * transfer functions.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_TRANSFER_H
#define DISCOFS_TRANSFER_H

#include "job.h"

#define TRANSFER_FAIL -1
#define TRANSFER_OK 0
#define TRANSFER_FINISH 1

/*! initialize transferring of a file */
int transfer_begin(struct job *j);

/*! start (or, if _from_ and _to_ are NULL, continue) transfering a file */
int transfer(const char *from, const char *to);

/*! update the currently transferred file's directory path */ 
void transfer_rename_dir(const char *from, const char *to);

/*! rename transferred file */
void transfer_rename(const char *to);

/*! abort the current transfer */
void transfer_abort(void);

/*! instantly copy a file from remote to cache */
int transfer_instant_pull(const char *path);

#endif
