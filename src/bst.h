/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#ifndef BST_H
#define BST_H

/********************************************/
/* simple binary search tree implementation */
/********************************************/


/*=============*/
/* DEFINITIONS */
/*=============*/

#ifndef bstdata_t
typedef unsigned long bstdata_t;
#endif

typedef struct bst bst;


/*====================*/
/* EXPORTED FUNCTIONS */ 
/*====================*/

/* initialize bst */
bst *bst_init(void);

/* free a bst */
void bst_free(bst *t);

/* free all nodes from tree; leaves an empty tree */
void bst_clear(bst *t);

/* insert an item into the tree */
int bst_insert(bst *t, bstdata_t data);
/* insert allowing duplicates */
int bst_insert_dup(bst *t, bstdata_t data);

/* delete item from tree */
int bst_delete(bst *t, bstdata_t data);
/* delete all equal items from tree */
int bst_delete_dup(bst *t, bstdata_t data);

/* check if tree contains equal item */
int bst_contains(bst *t, bstdata_t data);

#endif
