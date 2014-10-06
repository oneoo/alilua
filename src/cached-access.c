#include "cached-access.h"

static rb_tree_t rb_access_tree;

static int rb_access_tree_compare(const void *lhs, const void *rhs)
{
    int ret = 0;

    const rb_access_key_t *l = (const rb_access_key_t *)lhs;
    const rb_access_key_t *r = (const rb_access_key_t *)rhs;

    if(l->key > r->key) {
        ret = 1;

    } else if(l->key < r->key) {
        ret = -1;

    }

    return ret;
}

int init_access_cache()
{
    return (rb_tree_new(&rb_access_tree, rb_access_tree_compare) == RB_OK);
}

int cached_access(unsigned long key, const char *path)
{
    rb_access_key_t rbk = {0};
    rb_access_key_t *_rbk = NULL;
    rb_tree_node_t *tnode = NULL;
    rbk.key = key;

    if(rb_tree_find(&rb_access_tree, &rbk, &tnode) == RB_OK) {
        _rbk = (rb_access_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

        if(now - _rbk->last > 10) {
            _rbk->last = now;
            _rbk->exists = access(path, F_OK);
        }

        return _rbk->exists;

    } else {
        tnode = malloc(sizeof(rb_tree_node_t) + sizeof(rb_access_key_t));

        if(tnode) {
            memset(tnode, 0, sizeof(rb_tree_node_t) + sizeof(rb_access_key_t));

            _rbk = (rb_access_key_t *)((char *)tnode + sizeof(rb_tree_node_t));
            _rbk->key = key;
            _rbk->exists = access(path, F_OK);

            if(rb_tree_insert(&rb_access_tree, _rbk, tnode) != RB_OK) {
                free(tnode);
                _rbk = NULL;

            } else {
                return _rbk->exists;
            }
        }
    }

    return -1;
}
