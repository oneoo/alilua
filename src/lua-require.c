#include "config.h"
#include "main.h"
#include "network.h"
#include "lua-ext.h"

static char tbuf_4096[4096] = {0};
extern int code_cache_ttl;
static const int sentinel_ = 0;
static unsigned short require_loader_ttls[4096] = {0};
int lua_f_package_require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *cache_name = luaL_checkstring(L, 1);
    int skip_loaded = 0;

    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    if(epd) {
        memcpy(tbuf_4096, epd->vhost_root, epd->vhost_root_len);
        int l = sprintf(tbuf_4096 + epd->vhost_root_len, "/%s", name);
        tbuf_4096[epd->vhost_root_len + l] = '\0';
        cache_name = (const char *)&tbuf_4096;

        uint32_t k = fnv1a_32(cache_name, epd->vhost_root_len + l) % 4096;

        //printf("%u %u\n", ((unsigned short)now), require_loader_ttls[k]);
        if(code_cache_ttl > 0 && ((unsigned short)now) - require_loader_ttls[k] >= code_cache_ttl) {
            skip_loaded = 1;

            if(require_loader_ttls[k] == 0) {
                require_loader_ttls[k] = ((unsigned short)now) + (k % code_cache_ttl);

            } else {
                require_loader_ttls[k] = ((unsigned short)now);
            }
        }
    }

    int i;
    lua_settop(L, 1);  /* _LOADED table will be at index 2 */
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");

    if(skip_loaded == 0) {
        lua_getfield(L, 2, cache_name);

        if(lua_toboolean(L, -1)) {   /* is it there? */
            if(lua_touserdata(L, -1) == ((void *)&sentinel_)) { /* check loops */
                luaL_error(L, "loop or previous error loading module " LUA_QS, name);
            }

            return 1;  /* package is already loaded */
        }
    }

    /* else must load it; iterate over available loaders */
    lua_getfield(L, LUA_ENVIRONINDEX, "loaders");

    if(!lua_istable(L, -1)) {
        luaL_error(L, LUA_QL("package.loaders") " must be a table");
    }

    lua_pushliteral(L, "");  /* error message accumulator */

    for(i = 1; ; i++) {
        lua_rawgeti(L, -2, i);  /* get a loader */

        if(lua_isnil(L, -1))
            luaL_error(L, "module " LUA_QS " not found:%s",
                       name, lua_tostring(L, -2));

        lua_pushstring(L, name);
        lua_call(L, 1, 1);  /* call it */

        if(lua_isfunction(L, -1)) { /* did it find module? */
            break;    /* module loaded successfully */

        } else if(lua_isstring(L, -1)) { /* loader returned error message? */
            lua_concat(L, 2);    /* accumulate it */

        } else {
            lua_pop(L, 1);
        }
    }

    lua_pushlightuserdata(L, ((void *)&sentinel_));
    lua_setfield(L, 2, cache_name);  /* _LOADED[name] = sentinel */
    lua_pushstring(L, name);  /* pass name as argument to module */
    lua_call(L, 1, 1);  /* run loaded module */

    if(!lua_isnil(L, -1)) { /* non-nil return? */
        lua_setfield(L, 2, cache_name);    /* _LOADED[name] = returned value */
    }

    lua_getfield(L, 2, cache_name);

    if(lua_touserdata(L, -1) == ((void *)&sentinel_)) {    /* module did not set a value? */
        lua_pushboolean(L, 1);  /* use true as result */
        lua_pushvalue(L, -1);  /* extra copy to be returned */
        lua_setfield(L, 2, cache_name);  /* _LOADED[name] = true */
    }

    return 1;
}

