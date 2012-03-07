/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "bst.h"

#include <stdlib.h>
#include <errno.h>


/*========*/
/* macros */
/*========*/
#define BST_CMP(x, y) ((x < y) ? -1 : (x > y ? 1 : 0))

/*=========*/
/* structs */
/*=========*/

/* node in a bst */
typedef struct bstnode
{
    bstdata_t data;
    struct bstnode *parent;
    struct bstnode *left;
    struct bstnode *right;
} bstnode;

/* bst object, a pointer to it is the first argument to all bst_ functions */
struct bst
{
    bstnode *root;
};


/*===================*/
/* static prototypes */
/*===================*/

static bstnode *bstnode_init(bstdata_t data, bstnode *parent);
static void bstnode_free(bstnode *n);

static bstnode *bst_findpath(bstnode *n, bstdata_t data);
static int bst_insert_(bst *t, bstdata_t data, int dups);
static int bst_delete_(bst *t, bstdata_t data, int dups);
static bstnode *bst_delete_at(bstnode *n);


/*==================*/
/* static functions */
/*=================*/

/* create a new bst node */
static bstnode *bstnode_init(bstdata_t data, bstnode *parent)
{
    bstnode *n = malloc(sizeof(bstnode));

    if (n) {
        n->data = data;
        n->parent = parent;
        n->left = NULL;
        n->right = NULL;
    }
    return n;
}

/* free a bst node and all it's descendants */
static void bstnode_free(bstnode *n)
{
    if (!n)
        return;

    bstnode_free(n->left);
    bstnode_free(n->right);

    free(n);
}

/* find node in tree */
static bstnode *bst_findpath(bstnode *n, bstdata_t data)
{
    int cmp = BST_CMP(data, n->data);
    if (cmp == 0)
        return n;
    else if (cmp < 0)
        return (!n->left) ? n : bst_findpath(n->left, data);
    else
        return (!n->right) ? n : bst_findpath(n->right, data);
}

/* insert a node */
static int bst_insert_(bst *t, bstdata_t data, int dups)
{
    bstnode *n;
    bstnode *ins;
    int cmp;

    if (!t->root) {
        t->root = bstnode_init(data, NULL);
        if (!t->root) {
            errno = ENOMEM;
            return -1;
        }
        return 0;
    }

    n = bst_findpath(t->root, data);

    cmp = BST_CMP(data, n->data);

    /* another node with equal data exists */
    if (cmp == 0) {
        /* no duplicates allowed */
        if (!dups) {
            errno = EINVAL;
            return -1;
        }
        /* duplicates alloewd -> insert left */
        ins = bstnode_init(data, n);
        n->left = ins;
    }
    /* new data smaller -> insert left */
    else if (cmp < 0) {
        ins = bstnode_init(data, n);
        n->left = ins;
    }
    /* new data greater -> insert right */
    else {
        ins = bstnode_init(data, n);
        n->right = ins;
    }

    if (!ins) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

/* find node that replaces a node n, move it to n's position and return it */
static bstnode *bst_delete_at(bstnode *n)
{
    static int bst_del_from_left = 1;

    /* p will take n's position */
    bstnode *p;

    /* no children */
    if (!n->left && !n->right) {
        return NULL;
    }
    /* 1 child */
    else if (!n->left) {
        p = n->right;
    }
    else if (!n->right) {
        p = n->left;
    }

    /* 2 children */
    else {
        /* go either left or right way */
        bst_del_from_left ^= 1;

        /* n's predecessor replaces n */
        if (bst_del_from_left) {
            /* find predecessor (rightmost item of left subtree) */
            p = n->left;
            if (p->right) {
                while (p->right)
                    p = p->right;

                /* p's left subtree becomes his former parent's right subtree 
                   (p has no right subtree)
                 */
                if (p->left)
                    p->left->parent = p->parent;
                p->parent->right = p->left;

                /* p's new left subtree is n's subtree */
                p->left = n->left;
            }
            /* p's new left subtree is n's subtree */
            p->right = n->right;
        }
        /* analoguous to the above method */
        else {
            p = n->right;
            if (p->left) {
                while (p->left)
                    p = p->left;
                if (p->right)
                    p->right->parent = p->parent;
                p->parent->left = p->right;
                p->right = n->right;
            }
            p->left = n->left;
        }
    }

    /* update children's parent ptr */
    if (p->right)
        p->right->parent = p;
    if (p->left)
        p->left->parent = p;

    /* set parent */
    p->parent = n->parent;

    /* child ref of n's (now p's) parent update outside of this function */
    return p;
}

/* delete one/all nodes with the given data */
static int bst_delete_(bst *t, bstdata_t data, int dups)
{
    /* node to delete and it's parent */
    bstnode *del, *par;

    /* number of deleted nodes */
    int deleted = 0;

    /* empty tree */
    if (!t->root) {
        errno = EINVAL;
        return -1;
    }

    do {
        del = bst_findpath(t->root, data);

        /* no node found, return */
        if (BST_CMP(data, del->data) != 0) {
            return deleted;
        }

        par = del->parent;

        /* bst_delete_at returns node that will replace the deleted node
           uptdate del's parents (or t's root if del was the old root) */

        if (del == t->root)
            t->root = bst_delete_at(del);
        /* is left child */
        else if (del == par->left)
            par->left = bst_delete_at(del);
        /* is right child */
        else
            par->right = bst_delete_at(del);

        /* finally free it */
        free(del);

        deleted++;
    /* repeat if dups non-zero until no node found */
    } while (dups);

    return deleted;
}


/*====================*/
/* exported functions */
/*====================*/

bst *bst_init(void)
{
    bst *t = malloc(sizeof(bst));
    if (t) {
        t->root = NULL;
    }
    return 0;
}

void bst_free(bst *t)
{
    bst_clear(t);
    free(t);
}

void bst_clear(bst *t)
{
    if (!t || !t->root)
        return;

    bstnode_free(t->root);
    t->root = NULL;
}

int bst_empty(bst *t)
{
    return (t->root == NULL);
}

int bst_insert(bst *t, bstdata_t data)
{
    return bst_insert_(t, data, 0);
}

int bst_insert_dup(bst *t, bstdata_t data)
{
    return bst_insert_(t, data, 1);
}

int bst_delete(bst *t, bstdata_t data)
{
    return bst_delete_(t, data, 0);
}

int bst_delete_dup(bst *t, bstdata_t data)
{
    return bst_delete_(t, data, 1);
}

int bst_contains(bst *t, bstdata_t data)
{
    bstnode *n;

    if (!t || !t->root)
        return 0;

    n = bst_findpath(t->root, data);

    return (BST_CMP(data, n->data) == 0);
}
