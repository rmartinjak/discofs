#ifndef QUEUE_H
#define QUEUE_H

#define QUEUE_INIT { NULL, NULL }

struct qitem {
	void *data;
	struct qitem *next;
};

typedef struct queue {
	struct qitem *head;
	struct qitem *tail;
} queue;

int q_enqueue(queue *q, void *d);
void *q_dequeue(queue *q);
void q_clear(queue *q, int free_data);
void q_clear_cb(queue *q, int free_data, void (*cb)(void*));
int q_contains(queue *q, const void *d, int(*cmp)(const void*, const void*));
int q_contains2(queue *q, const void *d, int(*cmp)(const void*, const void*, void*), void *arg);
int q_empty(queue *q);

#endif
