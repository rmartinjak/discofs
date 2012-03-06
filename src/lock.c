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

static struct bst lock_tree;

int has_lock(const char *path, int type)
{
    int res;
    bstdata_t hash = djb2(path, SIZE_MAX);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);
    res = bst_contains(&lock_tree, hash);
    pthread_mutex_unlock(&m_lock_tree);

    return res;
}

int set_lock(const char *path, int type)
{
    int res;
    bstdata_t hash = djb2(path, SIZE_MAX);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);

    if (type == LOCK_OPEN)
        res = bst_insert_dup(&lock_tree, hash);
    else
        res = bst_insert(&lock_tree, hash);

    pthread_mutex_unlock(&m_lock_tree);

    return res;
}

int remove_lock(const char *path, int type)
{
    int res;
    bstdata_t hash = djb2(path, SIZE_MAX);

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);
    res = bst_delete(&lock_tree, hash);
    pthread_mutex_unlock(&m_lock_tree);

    return res;
}
