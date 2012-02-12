/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "hashtable.h"

#include <stdlib.h>
#include <errno.h>

/*static prototypes */
static htbucket *ht_alloc_buckets(int n);

/* HASHTABLE FUNCTIONS */

static htbucket *ht_alloc_buckets(int n) {
	htbucket *ret;
	htbucket *p;

	if ((ret = malloc(n * sizeof(htbucket))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	p = ret;

	while (n--) {
		p->root = NULL;
		++p;
	}

	return ret;
}

int ht_init(hashtable **ht, ht_hashfunc_t hash, ht_cmpfunc_t cmp) {
	*ht = malloc(sizeof(hashtable));

	if (*ht == NULL) {
		errno = EINVAL;
		return HT_ERROR;
	}

	(*ht)->n_items = 0;
	(*ht)->n_buckets = HT_BUCKETS_MIN;

	(*ht)->hash = hash;
	(*ht)->cmp = cmp;

	(*ht)->buckets = ht_alloc_buckets((*ht)->n_buckets);

	if ((*ht)->buckets)
		return HT_OK;
	else {
		free(*ht);
		errno = ENOMEM;
		return HT_ERROR;
	}
}

void ht_free(hashtable *ht, void (*free_key)(void*), void (*free_data)(void*)) {
	int n = ht->n_buckets;

	while (n--) {
		htbucket_clear(ht->buckets + n, free_key, free_data);
	}
	free(ht->buckets);
}

int ht_resize(hashtable *ht, int n) {
	unsigned int i;
	hash_t k;
	htbucket *newbuckets;
	void *key, *data;

	if (n > HT_BUCKETS_MAX || n < HT_BUCKETS_MIN)
		return HT_OK;

	if ((newbuckets = ht_alloc_buckets(n)) == NULL) {
		errno = ENOMEM;
		return HT_ERROR;
	}

	for (i=0; i<ht->n_buckets; ++i) {
		while (1) {
			htbucket_pop(ht->buckets + i, &key, &data);
			/* no more items in bucket */
			if (!key)
				break;
			/* store in new bucket */
			k = ht->hash(key, NULL) % n;
			htbucket_insert(newbuckets + k, key, data, ht->cmp, NULL);
		}
	}

	free(ht->buckets);
	ht->buckets = newbuckets;
	ht->n_buckets = n;

	return HT_OK;
}

int ht_insert(hashtable *ht, void *key, void *data, const void *hash_arg, const void *cmp_arg) {
	int res;
	hash_t k;

	if (!key || !data) {
		errno = EINVAL;
		return HT_ERROR;
	}

	k = ht->hash(key, hash_arg) % ht->n_buckets;

	res = htbucket_insert(ht->buckets + k, key, data, ht->cmp, cmp_arg);

	if (res == HT_OK) {
		ht->n_items++;
		if (ht->n_items > ht->n_buckets)
			ht_resize(ht, ht->n_buckets * 2);
	}

	return res;
}

void *ht_get(hashtable *ht, const void *key, const void *hash_arg, const void *cmp_arg) {
	hash_t k;
	k = ht->hash(key, hash_arg) % ht->n_buckets;
	return htbucket_get(ht->buckets + k, key, ht->cmp, cmp_arg);
}

void *ht_remove(hashtable *ht, const void *key, void (*free_key)(void*), const void *hash_arg, const void *cmp_arg) {
	hash_t k;
	void *res;

	if (!ht || !key || !free_key) {
		errno = EINVAL;
		return NULL;
	}

	k = ht->hash(key, hash_arg) % ht->n_buckets;
	res = htbucket_remove(ht->buckets + k, key, free_key, ht->cmp, cmp_arg);

	if (res) {
		ht->n_items--;
		/* items < buckets/4 -> resize */
		if (ht->n_items * 4 < ht->n_buckets) {
			ht_resize(ht, ht->n_buckets / 2);
		}
	}
	return res;
}

void ht_pop(hashtable *ht, void **key, void **data) {
	unsigned int i;
	for (i = 0; i < ht->n_buckets; i++) {
		if (!htbucket_empty(ht->buckets + i)) {
			htbucket_pop(ht->buckets + i, key, data);
			return;
		}
	}
}


/* BUCKET FUNCTIONS */

int htbucket_empty(htbucket *b) {
	return (b->root == NULL);
}

/* insert item into bucket */
int htbucket_insert(htbucket *b, void *key, void *data, ht_cmpfunc_t cmp, const void *cmp_arg) {
	struct htbucket_item *p, *ins;

	if (!b || !key || !data || !cmp) {
		errno = EINVAL;
		return HT_ERROR;
	}

	/* initialize item */
	ins = malloc(sizeof(struct htbucket_item));
	if (!ins) {
		errno = ENOMEM;
		return HT_ERROR;
	}
	ins->key = key;
	ins->data = data;
	ins->next = NULL;

	/* empty htbucket */
	if (!b->root) {
		b->root = ins;
		return HT_OK;
	}
	/* compare with root */
	else if (cmp(key, b->root->key, cmp_arg) == 0)
			return HT_EXIST;

	/* compare with child until p is last item */
	for (p = b->root; p->next; p = p->next)
		if (cmp(key, p->next->key, cmp_arg) == 0)
			return HT_EXIST;

	p->next = ins;
	return HT_OK;
}

/* return bucket item with given key */
void *htbucket_get(htbucket *b, const void *key, ht_cmpfunc_t cmp, const void *cmp_arg) {
	struct htbucket_item *p;

	if (!b || !key || !cmp) {
		errno = EINVAL;
		return NULL;
	}

	for (p = b->root; p; p = p->next) {
		if (cmp(key, p->key, cmp_arg) == 0) {
			return p->data;
		}
	}
	return NULL;
}

/* remove and return item with the given key */
void *htbucket_remove(htbucket *b, const void *key, void(*free_key)(void*), ht_cmpfunc_t cmp, const void *cmp_arg ) {
	struct htbucket_item *p, *del;
	void *ret;

	if (!b || !key || !cmp) {
		errno = EINVAL;
		return NULL;
	}

	if (!b->root)
		return NULL;
	else if (cmp(key, b->root->key, cmp_arg) == 0) {
		del = b->root;
		b->root = del->next;
		ret = del->data;
		free_key(del->key);
		free(del);
		return ret;
	}

	for (p = b->root; p->next; p = p->next) {
		if (cmp(key, p->next->key, cmp_arg) == 0) {
			del = p->next;
			p->next = del->next;
			ret = del->data;
			free_key(del->key);
			free(del);
			return ret;
		}
	}
	return NULL;
}

/* remove first item from bucket, storing key and data in the passed pointers */
void htbucket_pop(htbucket *b, void **key, void **data) {
	struct htbucket_item *p;

	if (!b->root) {
		*key = NULL;
		*data = NULL;
		return;
	}

	p = b->root;
	b->root = p->next;

	*key = p->key;
	*data = p->data;
	free(p);
}

/* remove all items from the bucket, using the passed free_*() functions on key and data */
void htbucket_clear(htbucket *b, void (*free_key)(void*),  void (*free_data)(void*)) {
	struct htbucket_item *p, *del;

	if (!b) {
		errno = EINVAL;
		return;
	}

	p = b->root;

	while (p) {
		del = p;
		p = p->next;

		if (free_key) free_key(del->key);
		if (free_data) free_data(del->data);
		free(del);
	}
}

htiter *ht_iter(hashtable *ht) {
	htiter *it;

	if ((it = malloc(sizeof(htiter)))) {
		it->ht = ht;
		it->b = 0;
		it->cur = NULL;
	}
	return it;
}

int htiter_next(htiter *it, void **key, void **data) {
	if (it->cur && it->cur->next) {
		it->cur = it->cur->next;
	}
	else {
		/* not the first item */
		if (it->cur)
			it->b++;

		while (it->b < it->ht->n_buckets && htbucket_empty(it->ht->buckets + it->b))
			it->b++;

		if (it->b >= it->ht->n_buckets) {
			*key = NULL;
			*data = NULL;
			return 0;
		}

		it->cur = (it->ht->buckets + it->b)->root;
	}

	if (key)
		*key = it->cur->key;
	if (data)
		*data = it->cur->data;
	return 1;
}
