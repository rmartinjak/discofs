/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef BST_H
#define BST_H

/* simple binary search tree implementation */


#include <stdlib.h>

#define BST_INIT { NULL }
#define CMP(x, y) ((x < y) ? -1 : (x > y ? 1 : 0 ))

#ifndef bstdata_t
typedef unsigned long bstdata_t;
#endif

struct bstnode {
	bstdata_t data;
	struct bstnode *parent;
	struct bstnode *left;
	struct bstnode *right;
};

struct bst {
	struct bstnode *root;
};

#define bst_empty(t) ((t)->root == NULL)

#define bst_insert(t, data) bst_insert_(t, data, 0)
#define bst_insert_dup(t, data) bst_insert_(t, data, 1)
int bst_insert_(struct bst *t, bstdata_t data, int allow_dups);

#define bst_delete(t, data) bst_delete_(t, data, 0)
#define bst_delete_all(t, data) bst_delete_(t, data, 1)
int bst_delete_(struct bst *t, bstdata_t data, int delete_dups);
int bst_contains(struct bst *t, bstdata_t data);
void bst_clear(struct bst *t);
#endif
