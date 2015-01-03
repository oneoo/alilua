#include "main.h"
#include "../coevent/merry/common/rbtree.h"

#define MAX_LUA_THREAD_COUNT 10000

typedef struct lua_thread_link_s {
    lua_State *L;
    struct lua_thread_link_s *next;
} lua_thread_link_t;

static int lua_thread_count = 0;
static lua_thread_link_t *lua_thread_head = NULL;
static lua_thread_link_t *lua_thread_tail = NULL;

static rb_tree_t coroutine_tree;
static int lua_thread_inited = 0;

typedef struct {
    lua_State *co;
    void *gk;
} rb_key_t;

typedef struct {
    char k[56];
    void *next;
} gk_key_t;

static int corotinue_rbtree_compare(const void *lhs, const void *rhs)
{
    int ret = 0;

    const rb_key_t *l = (const rb_key_t *)lhs;
    const rb_key_t *r = (const rb_key_t *)rhs;

    if(l->co > r->co) {
        ret = 1;

    } else if(l->co < r->co) {
        ret = -1;

    }

    return ret;
}

static rb_key_t *get_thread_rbtree_key(lua_State *co)
{
    rb_key_t *key = NULL, _key;
    rb_tree_node_t *tnode = NULL;
    _key.co = co;

    if(rb_tree_find(&coroutine_tree, &_key, &tnode) == RB_OK) {
        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

    } else {
        tnode = malloc(sizeof(rb_tree_node_t) + sizeof(rb_key_t));
        memset(tnode, 0, sizeof(rb_tree_node_t) + sizeof(rb_key_t));

        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

        key->co = co;

        if(rb_tree_insert(&coroutine_tree, key, tnode) != RB_OK) {
            free(tnode);
            tnode = NULL;
            key = NULL;
        }
    }

    return key;
}


void reinit_lua_thread_env(lua_State *L)
{
    rb_key_t *key = get_thread_rbtree_key(L);
    gk_key_t *gk = key->gk, *bf = NULL;
    key->gk = NULL;

    lua_settop(L, lua_gettop(L));

    lua_getglobal(L, "_G");

    while(gk) {
        lua_pushstring(L, gk->k);
        lua_pushnil(L);
        lua_rawset(L, -3);

        bf = gk;
        gk = gk->next;
        free(bf);
    }

    lua_pop(L, 1);
}

void release_lua_thread(lua_State *L)
{
    lua_thread_link_t *l = malloc(sizeof(lua_thread_link_t));

    if(!l) {
        LOGF(ERR, "malloc error!");
        return;
    }

    l->L = L;
    l->next = NULL;

    if(lua_thread_tail) {
        lua_thread_tail->next = l;

    } else {
        lua_thread_head = l;
    }

    lua_thread_tail = l;
}

static int l_env_newindex(lua_State *L)
{
    size_t len = 0;

    if(!lua_isstring(L, 2)) {
        return 0;
    }

    const char *key = lua_tolstring(L, 2, &len);
    lua_settop(L, 3);
    lua_rawset(L, -3);

    if(lua_thread_inited && len < 56 && (len < 2 || (key[0] != '_' && key[1] != '_'))) {
        rb_key_t *_key = get_thread_rbtree_key(L);

        if(_key) {
            gk_key_t *gk = malloc(sizeof(gk_key_t));
            memcpy(gk->k, key, len);
            gk->k[len] = '\0';
            gk->next = _key->gk;
            _key->gk = gk;
        }
    }

    return 0;
}

lua_State *new_lua_thread(lua_State *_L)
{
    lua_State *L = NULL;

    if(lua_thread_head) {
        L = lua_thread_head->L;
        void *l = lua_thread_head;
        lua_thread_head = lua_thread_head->next;

        if(lua_thread_tail == l) {
            lua_thread_head = NULL;
            lua_thread_tail = NULL;
        }

        free(l);
        return L;
    }

    if(lua_thread_count >= MAX_LUA_THREAD_COUNT) {
        LOGF(ERR, "Lua thread pool full!");
        return NULL;
    }

    lua_thread_inited = 0;

    lua_thread_count++;
    ///lua_getglobal(_L, "cothreads");
    L = lua_newthread(_L);
    ///lua_rawseti(_L, -2, lua_thread_count);
    ///lua_pop(_L, 1);

    int m_refkey = luaL_ref(_L, LUA_REGISTRYINDEX);
    // when you want to kill it.
    //lua_unref(m_state, m_refkey);
    lua_createtable(L, 0, 100);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");

    lua_newtable(L);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_settable(L, -3);

    lua_pushcfunction(L, l_env_newindex);
    lua_setfield(L, -2, "__newindex");

    lua_setmetatable(L, -2);
    lua_replace(L, LUA_GLOBALSINDEX);

    lua_getglobal(L, "__main");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfenv(L, -2);

    if(lua_resume(L, 0) == LUA_ERRRUN) {
        if(lua_isstring(L, -1)) {
            LOGF(ERR, "Lua:error %s", lua_tostring(L, -1));
        }

        lua_pop(L, 1);
    }

    lua_thread_inited = 1;

    return L;
}

void init_lua_threads(lua_State *_L, int count)
{
    rb_tree_new(&coroutine_tree, corotinue_rbtree_compare);

    int i = 0;
    void *LS = malloc(sizeof(void *)*count);

    for(i = 0; i < count; i++) {
        void **L = LS + (i * sizeof(void *));
        *L = new_lua_thread(_L);
    }

    for(i = 0; i < count; i++) {
        void **L = LS + (i * sizeof(void *));

        if(*L) {
            release_lua_thread(*L);
        }
    }

    free(LS);

    lua_thread_inited = 1;
}
