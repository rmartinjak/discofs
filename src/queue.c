/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "queue.h"

#include <stdlib.h>
#include <errno.h>

int q_enqueue(queue *q, void *d)
{
    struct qitem *ins;
    ins = malloc(sizeof (struct qitem));
    if (!ins) {
        errno = ENOMEM;
        return -1;
    }
    ins->data = d;
    ins->next = NULL;

    if (!q->head)
        q->head = ins;
    else
        q->tail->next = ins;

    q->tail = ins;
    return 0;
}

void *q_dequeue(queue *q)
{
    void *ret;
    struct qitem *del;

    if (!q->head)
        return NULL;

    ret = q->head->data;
    del = q->head;

    q->head = q->head->next;

    if (!del->next)
        q->tail = NULL;

    free(del);

    return ret;
}

void q_clear(queue *q, int free_data)
{
    void *del;
    while (q->head) {
        del = q_dequeue(q);
        if (free_data)
            free(del);
    }
}

void q_clear_cb(queue *q, int free_data, void (*cb)(void*))
{
    void *del;
    while (q->head) {
        del = q_dequeue(q);
        cb(del);
        if (free_data)
            free(del);
    }
}

int q_contains(queue *q, const void *d, int(*cmp)(const void*, const void*))
{
    struct qitem *p;
    for (p = q->head; p; p = p->next) {
        if (cmp(d, p->data) == 0)
            return 1;
    }

    return 0;
}

int q_contains2(queue *q, const void *d, int(*cmp)(const void*, const void*, void*), void *arg)
{
    struct qitem *p;
    for (p = q->head; p; p = p->next) {
        if (cmp(d, p->data, arg) == 0)
            return 1;
    }

    return 0;
}

int q_empty(queue *q)
{
    return (q->head == NULL);
}
