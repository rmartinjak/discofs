/* discofs - disconnected file system
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef DISCOFS_WORKER_H
#define DISCOFS_WORKER_H
#include "config.h"

#include "queue.h"
#include "job.h"

#include <pthread.h>

#define SLEEP_LONG 5
#define SLEEP_SHORT 2

void worker_wakeup();
void worker_sleep(unsigned int seconds);

void worker_block();
void worker_unblock();
int worker_blocked();

void worker_cancel_scan(void);

void *worker_statecheck(void *arg);
void *worker_main(void *arg);
#endif
