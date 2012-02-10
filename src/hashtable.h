#ifndef HASHTABLE_H
#define HASHTABLE_H

#define HT_OK 0
#define HT_ERROR -1
#define HT_EXIST 1

#ifndef HT_BUCKETS_MIN
	#define HT_BUCKETS_MIN (1 << 10)
#endif

#ifndef HT_BUCKETS_MAX
	#define HT_BUCKETS_MAX (1 << 16)
#endif

typedef unsigned int hash_t;
typedef hash_t(*ht_hashfunc_t)(const void*, const void*);
typedef int(*ht_cmpfunc_t)(const void*, const void*, const void*);

typedef struct hashtable {
	ht_hashfunc_t hash;
	ht_cmpfunc_t cmp;
	unsigned int n_items;
	unsigned int n_buckets;
	struct htbucket *buckets;
} hashtable;

typedef struct htbucket {
	struct htbucket_item *root;
} htbucket;

struct htbucket_item {
	void *key;
	void *data;
	struct htbucket_item *next;
};

typedef struct htiter {
	struct hashtable *ht;
	unsigned int b;
	struct htbucket_item *cur;
} htiter;

/* HASHTABLE FUNCTIONS */

int ht_init_(hashtable **ht,
			ht_hashfunc_t hash;
			ht_cmpfunc_t cmp;
			);
#define ht_init(ht, hash, cmp) ht_init_(ht, (ht_hashfunc_t)hash, (ht_cmpfunc_t)cmp)
void ht_free_(hashtable *ht, void (*free_key)(void*), void (*free_data)(void*));
#define ht_free(ht, free_key, free_data) ht_free_(ht, (void (*)(void*))free_key, (void (*)(void*))free_data);

int ht_resize(hashtable *ht, int n);

int ht_insert(hashtable *ht, void *key, void *data, const void *hash_arg, const void *cmp_arg);
void *ht_get(hashtable *ht, const void *key, const void *hash_arg, const void *cmp_arg);
void *ht_remove(hashtable *ht, const void *key, void (*free_key)(void*), const void *hash_arg, const void *cmp_arg);
#define ht_empty(ht) (ht->n_items == 0)
void ht_pop(hashtable *ht, void **key, void **data);

int htbucket_empty(htbucket *b);
int htbucket_insert(htbucket *b, void *key, void *data, ht_cmpfunc_t cmp, const void *cmp_arg);
void *htbucket_get(htbucket *b, const void *key, ht_cmpfunc_t cmp, const void *cmp_arg);
void *htbucket_remove(htbucket *b, const void *key, void (*free_key)(void*), ht_cmpfunc_t cmp, const void *cmp_arg);
void htbucket_pop(htbucket *b, void **key, void **data);
void htbucket_clear(htbucket *b, void (*free_key)(void*), void (*free_data)(void*));

htiter *ht_iter(hashtable *ht);
int htiter_next_(htiter *it, void **key, void **data);
#define htiter_next(it, key, data) htiter_next_(it, (void**)key, (void**)data)

#endif
