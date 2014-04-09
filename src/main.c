#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "config.h"
#include "vhost.h"
#include "worker.h"
#include "network.h"
#include "websocket.h"
#include "lua-ext.h"
#include "lua-ext-cache.h"

/// yac config
#ifdef linux
int YAC_CACHE_SIZE = (1024 * 1024 * 32);
#else /// for mac os
int YAC_CACHE_SIZE = (1024 * 1024 * 2);
#endif

lua_State *_L;
logf_t *ACCESS_LOG = NULL;
static char tbuf_4096[4096];

static void on_master_exit_handler()
{
    LOGF(ALERT, "master exited");
    shm_free(_shm_serv_status);
    log_destory(ACCESS_LOG);
}

static void master_main()
{
    //printf("master\n");
}

static void help()
{
    bind_port = default_port;
    printf("--------------------------------------------------------------------------------\n"
           "This is %s/%s , Usage:\n"
           "\n"
           "    %s [options]\n"
           "\n"
           "Options:\n"
           "\n"
           "    --bind=127.0.0.1:%d\tserver bind. or --bind=%d = 0.0.0.0:19827\n"
           "    --log=path[,level] \t\tlog file path, level=1-6\n"
           "    --accesslog=path \t\taccess log file path\n"
           "    --process=n \t\tstart how many workers\n"
           "    --host-route=path \t\tSpecial route file path\n"
           "    --app=path \t\t\tSpecial app file path\n"
           "    --code-cache-ttl \t\tnumber of code cache time(sec) default 60 sec\n"
           "    --cache-size \t\tsize of YAC shared memory cache (1m or 4096000k)\n"
           "    --daemon[=n]     \t\tdaemon mode(start n workers)\n"
           "  \n"
           "--------------------------------------------------------------------------------\n",
           program_name, "0.1", program_name, bind_port, bind_port
          );
}

int main(int argc, const char **argv)
{
    bind_port = default_port;
    char *cwd = init_process_title(argc, argv);
    char *msg = NULL;
    update_time();

    if(getarg("cache-size")) {
        msg = getarg("cache-size");
        YAC_CACHE_SIZE = atoi(msg);

        if(msg[strlen(msg) - 1] == 'm') {
            YAC_CACHE_SIZE = YAC_CACHE_SIZE * 1024 * 1024;

        } else if(msg[strlen(msg) - 1] == 'k') {
            YAC_CACHE_SIZE = YAC_CACHE_SIZE * 1024;
        }

    }

    if(YAC_CACHE_SIZE < 1024 * 1024 * 2) {
        YAC_CACHE_SIZE = 1024 * 1024 * 2;
    }

    if(!yac_storage_startup(YAC_CACHE_SIZE / 16, YAC_CACHE_SIZE - (YAC_CACHE_SIZE / 16), &msg)) {
        LOGF(ERR, "Shared memory allocator startup failed at '%s': %s", msg, strerror(errno));
        exit(1);
    }

    lua_State *L = luaL_newstate();

    if(!L) {
        LOGF(ERR, "error for luaL_newstate\n");
        exit(1);
    }

    luaL_openlibs(L);
    lua_getglobal(L, "_VERSION");
    const char *lua_ver = lua_tostring(L, -1);
    lua_getglobal(L, "jit");

    if(lua_istable(L, -1)) {
        lua_getfield(L, -1, "version");

        if(lua_isstring(L, -1)) {
            lua_ver = lua_tostring(L, -1);
        }
    }

    sprintf(hostname, "%s", lua_ver);
    lua_close(L);

    _L = luaL_newstate();
    lua_gc(_L, LUA_GCSTOP, 0);
    luaL_openlibs(_L);    /* Load Lua libraries */
    lua_gc(_L, LUA_GCRESTART, 0);

    if(getarg("code-cache-ttl")) {       /// default = 60s
        lua_pushnumber(_L, atoi(getarg("code-cache-ttl")));
        lua_setglobal(_L, "CODE_CACHE_TTL");
    }

    if(getarg("host-route")) {
        lua_pushstring(_L, getarg("host-route"));
        lua_setglobal(_L, "HOST_ROUTE");
    }

    if(!update_vhost_routes(getarg("host-route")) && !getarg("app")) {
        LOGF(WARN, "no host-rout or app arguments! using defalut settings.");
    }

    lua_register(_L, "echo", lua_echo);
    lua_register(_L, "sendfile", lua_sendfile);
    lua_register(_L, "header", lua_header);
    lua_register(_L, "clear_header", lua_clear_header);
    lua_register(_L, "__end", lua_end);
    lua_register(_L, "die", lua_die);
    lua_register(_L, "get_post_body", lua_get_post_body);
    lua_register(_L, "check_timeout", lua_check_timeout);
    lua_register(_L, "is_websocket", lua_f_is_websocket);
    lua_register(_L, "upgrade_to_websocket", lua_f_upgrade_to_websocket);
    lua_register(_L, "websocket_send", lua_f_websocket_send);
    lua_register(_L, "check_websocket_close", lua_f_check_websocket_close);
    lua_register(_L, "sleep", lua_f_sleep);

    lua_register(_L, "router", lua_f_router);

    lua_register(_L, "random_string", lua_f_random_string);
    lua_register(_L, "file_exists", lua_f_file_exists);
    lua_register(_L, "readfile", lua_f_readfile);

    lua_register(_L, "cache_set", lua_f_cache_set);
    lua_register(_L, "cache_get", lua_f_cache_get);
    lua_register(_L, "cache_del", lua_f_cache_del);

    luaopen_fastlz(_L);
    luaopen_coevent(_L);
    luaopen_libfs(_L);
    luaopen_string_utils(_L);
    luaopen_crypto(_L);

    lua_pop(_L, 1);

    sprintf(tbuf_4096,
            "package.path = '%s/lua-libs/?.lua;' .. package.path package.cpath = '%s/lua-libs/?.so;' .. package.cpath", cwd, cwd);
    luaL_dostring(_L, tbuf_4096);

    lua_pushstring(_L, "__main");

    luaL_dostring(_L, ""
                  "_router = router " \
                  "function router(u,t) local f,p = _router(u,t) if f then f(p) return true else return nil end end " \
                  "function cacheTable(ttl) " \
                  "    if not ttl or type(ttl) ~= 'number' or ttl < 2 then " \
                  "        local t = {} " \
                  "        local mt = {__newindex = function (t1,k,v) return false end} " \
                  "        setmetatable(t, mt) " \
                  "        return t " \
                  "    end " \
                  "    ttl = ttl/2 " \
                  "    local t = {{},{},{}} " \
                  "    local proxy = {} " \
                  "    local mt = { " \
                  "        __index = function (t1,k) " \
                  "            local p = math.floor(os.time()/ttl) " \
                  "            if t[(p-2)%3+1].__has then t[(p-2)%3+1] = {} end " \
                  "            return t[(p)%3+1][k] " \
                  "        end, " \
                  "        __newindex = function (t1,k,v) " \
                  "            local p = math.floor(os.time()/ttl) " \
                  "            t[p%3+1][k] = v " \
                  "            t[(p+1)%3+1][k] = v " \
                  "            t[p%3+1].__has = 1 " \
                  "        end " \
                  "    } " \
                  "    setmetatable(proxy, mt) " \
                  "    return proxy " \
                  "end " \
                  "if not CODE_CACHE_TTL then CODE_CACHE_TTL = 60 end " \
                  "CodeCache = cacheTable(CODE_CACHE_TTL) " \
                  "FileExistsCache = cacheTable(CODE_CACHE_TTL/2)"
                 );

    if(luaL_loadfile(_L, "core.lua")) {
        LOGF(ERR, "Couldn't load file: %s\n", lua_tostring(_L, -1));
        exit(1);
    }

    lua_rawset(_L, LUA_GLOBALSINDEX);

    if(getarg("accesslog")) {
        ACCESS_LOG = open_log(getarg("accesslog"), 40960);

        if(!ACCESS_LOG) {
            LOGF(ERR, "Couldn't open access log file: %s", getarg("accesslog"));
        }
    }

    _shm_serv_status = shm_malloc(sizeof(serv_status_t));

    attach_on_exit(on_master_exit_handler);
    return merry_start(argc, argv, help, master_main, on_master_exit_handler, worker_main, 0);
}
