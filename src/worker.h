/*! @file
 * worker functions.
 * discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_WORKER_H
#define DISCOFS_WORKER_H
#include "config.h"

#include "queue.h"
#include "job.h"

#include <pthread.h>

void worker_wakeup(void);
void worker_sleep(unsigned int seconds);

void worker_block(void);
void worker_unblock(void);
int worker_blocked(void);

void worker_cancel_scan(void);

void *worker_statecheck(void *arg);
void *worker_main(void *arg);
#endif
