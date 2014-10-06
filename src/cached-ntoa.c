#include "cached-ntoa.h"

static rb_tree_t rb_ntoa_tree;

static int rb_ntoa_tree_compare(const void *lhs, const void *rhs)
{
    int ret = 0;

    const rb_ntoa_key_t *l = (const rb_ntoa_key_t *)lhs;
    const rb_ntoa_key_t *r = (const rb_ntoa_key_t *)rhs;

    if(l->key > r->key) {
        ret = 1;

    } else if(l->key < r->key) {
        ret = -1;

    }

    return ret;
}

int init_ntoa_cache()
{
    return (rb_tree_new(&rb_ntoa_tree, rb_ntoa_tree_compare) == RB_OK);
}

const char *cached_ntoa(struct in_addr addr)
{
    rb_ntoa_key_t rbk = {0};
    rb_ntoa_key_t *_rbk = NULL;
    rb_tree_node_t *tnode = NULL;
    rbk.key = addr.s_addr;

    if(rb_tree_find(&rb_ntoa_tree, &rbk, &tnode) == RB_OK) {
        _rbk = (rb_ntoa_key_t *)((char *)tnode + sizeof(rb_tree_node_t));
        return _rbk->addr;

    } else {
        tnode = malloc(sizeof(rb_tree_node_t) + sizeof(rb_ntoa_key_t));

        if(tnode) {
            memset(tnode, 0, sizeof(rb_tree_node_t) + sizeof(rb_ntoa_key_t));

            _rbk = (rb_ntoa_key_t *)((char *)tnode + sizeof(rb_tree_node_t));
            _rbk->key = addr.s_addr;
            char *_t = inet_ntoa(addr);
            memcpy(_rbk->addr, _t, strlen(_t));

            if(rb_tree_insert(&rb_ntoa_tree, _rbk, tnode) != RB_OK) {
                free(tnode);
                _rbk = NULL;

            } else {
                return _rbk->addr;
            }
        }
    }

    return inet_ntoa(addr);
}
