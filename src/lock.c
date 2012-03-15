/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "config.h"
#include "lock.h"

#include "bst.h"
#include "funcs.h"
#include "log.h"

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

static pthread_mutex_t m_lock_tree = PTHREAD_MUTEX_INITIALIZER;

static bst *lock_tree = NULL;

int has_lock(const char *path, int type)
{
    int res;
    long hash = djb2(path, SIZE_MAX);

    if (!lock_tree)
        lock_tree = bst_init(NULL);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);
    res = bst_contains(lock_tree, hash);
    pthread_mutex_unlock(&m_lock_tree);

    return res;
}

int set_lock(const char *path, int type)
{
    int res;
    long hash = djb2(path, SIZE_MAX);

    if (!lock_tree)
        lock_tree = bst_init(NULL);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    DEBUG("setting lock %s on %s\n", (type == LOCK_OPEN) ? "LOCK_OPEN" : "LOCK_TRANSFER", path);

    pthread_mutex_lock(&m_lock_tree);

    if (type == LOCK_OPEN)
        res = bst_insert_dup(lock_tree, hash, NULL);
    else
        res = bst_insert(lock_tree, hash, NULL);

    pthread_mutex_unlock(&m_lock_tree);

    return res;
}

int remove_lock(const char *path, int type)
{
    int res;
    long hash = djb2(path, SIZE_MAX);

    if (!lock_tree)
        lock_tree = bst_init(NULL);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    DEBUG("removing lock %s on %s\n", (type == LOCK_OPEN) ? "LOCK_OPEN" : "LOCK_TRANSFER", path);

    pthread_mutex_lock(&m_lock_tree);
    res = bst_remove(lock_tree, hash, NULL);
    pthread_mutex_unlock(&m_lock_tree);

    return res;
}
