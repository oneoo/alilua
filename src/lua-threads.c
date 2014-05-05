#include "main.h"

#define MAX_LUA_THREAD_COUNT 10000

typedef struct lua_thread_link_s {
    lua_State *L;
    struct lua_thread_link_s *next;
} lua_thread_link_t;

static int lua_thread_count = 0;
static lua_thread_link_t *lua_thread_head = NULL;
static lua_thread_link_t *lua_thread_tail = NULL;

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

    lua_thread_count++;
    L = lua_newthread(_L);
    int m_refkey = luaL_ref(_L, LUA_REGISTRYINDEX);
    // when you want to kill it.
    //lua_unref(m_state, m_refkey);

    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");

    lua_newtable(L);
    /*
    lua_getglobal(L, "__newindex_handle");
    lua_setfield(L, -2, "__newindex");
    */
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_settable(L, -3);
    lua_setmetatable(L, -2);
    lua_replace(L, LUA_GLOBALSINDEX);

    lua_getglobal(L, "__main");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfenv(L, -2);

    lua_settop(L, -1);

    if(lua_resume(L, 0) != LUA_YIELD) {
        if(lua_isstring(L, -1)) {
            LOGF(ERR, "Lua:error %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    return L;
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
