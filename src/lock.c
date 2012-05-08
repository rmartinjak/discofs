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
        lock_tree = bst_init();

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
        lock_tree = bst_init();

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);

    if (type == LOCK_OPEN)
    {
        size_t *c;
        c = bst_get(lock_tree, hash);
        if (!c)
        {
            if ((c = malloc(sizeof *c)))
            {
                *c = 0;
                res = bst_insert(lock_tree, hash, c);
            }
            else
                res = -1;
        }
        else
        {
            *c += 1;
            res = 0;
        }
    }
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
        lock_tree = bst_init();

    hash <<= LOCK_TYPE_BITS;
    hash |= type;

    pthread_mutex_lock(&m_lock_tree);

    if (type == LOCK_OPEN)
    {
        size_t *c;
        c = bst_get(lock_tree, hash);
        if (c)
        {
            res = 0;
            if (--(*c) == 0)
                res = bst_remove(lock_tree, hash, free);
        }
        else
            res = -1;
    }
    else
        res = bst_remove(lock_tree, hash, NULL);

    pthread_mutex_unlock(&m_lock_tree);

    return res;
}
