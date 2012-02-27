/* fs2go - takeaway filesystem
 * Copyright (c) 2012 Robin Martinjak
 * see LICENSE for full license (BSD 2-Clause)
 */

#include "bst.h"

#include <stdlib.h>
#include <errno.h>

static struct bstnode *bst_findpath(struct bstnode *n, bstdata_t data)
{
    int cmp = CMP(data, n->data);
    if (cmp == 0)
        return n;
    else if (cmp < 0)
        return (!n->left) ? n : bst_findpath(n->left, data);
    else
        return (!n->right) ? n : bst_findpath(n->right, data);
}

int bst_insert_(struct bst *t, bstdata_t data, int allow_dups)
{
    struct bstnode *n;
    struct bstnode *ins;
    int cmp;

    if (!t->root) {
        t->root = malloc(sizeof(struct bstnode));
        if (!t->root) {
            errno = ENOMEM;
            return -1;
        }

        t->root->data = data;
        t->root->left = NULL;
        t->root->right = NULL;
        t->root->parent = NULL;
        return 0;
    }

    n = bst_findpath(t->root, data);

    cmp = CMP(data, n->data);

#define NODEINS(dir) if (!(ins = malloc(sizeof(struct bstnode)))) { errno = ENOMEM; return -1; } \
    ins->data = data; \
    ins->parent = n; \
    ins->left = NULL; \
    ins->right = NULL; \
    n->dir = ins; \
    return 0;

    if (cmp == 0) {
        if (!allow_dups) {
            errno = EINVAL;
            return -1;
        }
        NODEINS(left);
    }
    else if (cmp < 0) {
        NODEINS(left);
    }
    else {
        NODEINS(right);
    }
#undef NODEINS
}

static int bst_del_from_left = 1;
static struct bstnode *bst_delete_at(struct bstnode *n)
{
    struct bstnode *p;

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
        if (bst_del_from_left) {
            p = n->left;
            if (p->right) {
                while (p->right)
                    p = p->right;
                if (p->left)
                    p->left->parent = p->parent;
                p->parent->right = p->left;
                p->left = n->left;
            }
            p->right = n->right;
        }
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

    if (p->right)
        p->right->parent = p;
    if (p->left)
        p->left->parent = p;
    p->parent = n->parent;
    return p;
}

int bst_delete_(struct bst *t, bstdata_t data, int delete_dups)
{
    struct bstnode *del;
    struct bstnode *par;

    /* empty */
    if (!t->root)
        return -1;

    do {
        del = bst_findpath(t->root, data);

        /* data not found */
        if (CMP(data, del->data) != 0) {
            return (delete_dups) ? 0 : -1;
        }

        par = del->parent;
        if (del == t->root)
            t->root = bst_delete_at(del);
        /* is left child */
        else if (del == par->left)
            par->left = bst_delete_at(del);
        /* is right child */
        else
            par->right = bst_delete_at(del);

        free(del);

    } while (delete_dups);

    return 0;
}

int bst_contains(struct bst *t, bstdata_t data)
{
    struct bstnode *n;

    if (!t || !t->root)
        return 0;

    n = bst_findpath(t->root, data);

    return (CMP(data, n->data) == 0);
}

static void bst_clear_at(struct bstnode *n)
{
    if (n->left)
        bst_clear_at(n->left);
    if (n->right)
        bst_clear_at(n->right);
    free(n);
}

void bst_clear(struct bst *t)
{
    if (!t || !t->root)
        return;

    bst_clear_at(t->root);
}
