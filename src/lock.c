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
static pthread_mutex_t m_lock_transfer = PTHREAD_MUTEX_INITIALIZER;

static bst *lock_tree = NULL;
static char *lock_transfer = NULL;

int lock_init(void)
{
    lock_tree = bst_init();

    if (!lock_tree)
        return -1;

    return 0;
}

void lock_destroy(void)
{
    bst_free(lock_tree, &free);
    free(lock_transfer);
}

int lock_has(const char *path, int type)
{
    int res;
    if (type == LOCK_OPEN)
    {
        long hash = djb2(path, SIZE_MAX);
        pthread_mutex_lock(&m_lock_tree);
        res = bst_contains(lock_tree, hash);
        pthread_mutex_unlock(&m_lock_tree);
    }
    else
        res = (lock_transfer && !strcmp(path, lock_transfer));

    return res;
}

int lock_set(const char *path, int type)
{
    int res;

    if (type == LOCK_OPEN)
    {
        unsigned int *c;
        long hash = djb2(path, SIZE_MAX);

        pthread_mutex_lock(&m_lock_tree);

        if ((c = bst_get(lock_tree, hash)))
        {
            *c += 1;
            res = 0;
        }
        else
        {
            if ((c = malloc(sizeof *c)))
            {
                *c = 1;
                res = bst_insert(lock_tree, hash, c);
            }
            else
                res = -1;
        }

        pthread_mutex_unlock(&m_lock_tree);
    }
    else
    {
        pthread_mutex_lock(&m_lock_transfer);

        if (lock_transfer)
            res = -1;
        else
        {
            lock_transfer = strdup(path);
            res = (lock_transfer) ? 0 : -1;
        }

        pthread_mutex_unlock(&m_lock_transfer);
    }

    return res;
}

int lock_remove(const char *path, int type)
{
    int res;

    if (type == LOCK_OPEN)
    {
        unsigned int *c;
        long hash = djb2(path, SIZE_MAX);

        pthread_mutex_lock(&m_lock_tree);

        if ((c = bst_get(lock_tree, hash)))
        {
            res = 0;
            if (--(*c) == 0)
                res = bst_remove(lock_tree, hash, free);
        }
        else
            res = -1;

        pthread_mutex_unlock(&m_lock_tree);
    }
    else
    {
        if (!lock_transfer)
            return -1;

        pthread_mutex_lock(&m_lock_transfer);

        free(lock_transfer);
        lock_transfer = NULL;

        pthread_mutex_unlock(&m_lock_transfer);

        res = 0;
    }
    return res;
}
