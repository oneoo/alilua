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

SSL_CTX *ssl_ctx = NULL;
int ssl_epd_idx = -1;

lua_State *_L = NULL;
logf_t *ACCESS_LOG = NULL;
static char tbuf_4096[4096] = {0};

extern int code_cache_ttl;
extern char process_chdir[924];

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

static int verify_callback(int ok, X509_STORE_CTX *store)
{
    epdata_t *epd = SSL_get_ex_data(X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()), ssl_epd_idx);
    epd->ssl_verify = ok;

    return ok;
}

static void help()
{
    bind_port = default_port;
    printf("--------------------------------------------------------------------------------\n"
           "           _________________                \n"
           "    ______ ___  /___(_)__  / ____  _______ _\n"
           "    _  __ `/_  / __  /__  /  _  / / /  __ `/\n"
           "    / /_/ /_  /___  / _  /___/ /_/ // /_/ / \n"
           "    \\__,_/ /_____/_/  /_____/\\__,_/ \\__,_/  \n"
           "--------------------------------------------------------------------------------\n"
           "This is %s/%s, Usage:\n"
           "\n"
           "    %s [options]\n"
           "\n"
           "Options:\n"
           "\n"
           "    --bind=127.0.0.1:%d\tserver bind. or --bind=%d = 0.0.0.0:19827\n"
           "    --daemon[=n]     \t\tdaemon mode(start n workers)\n"
           "    --thread=n       \t\tnumber of Lua coroutines per worker\n"
           "    --ssl-bind\t\t\tssl server bind.\n"
           "    --ssl-cert\t\t\tssl Certificate file path\n"
           "    --ssl-key\t\t\tssl PrivateKey file path\n"
           "    --ssl-ca\t\t\tssl Client Certificate file path\n"
           "    --log=path[,level] \t\tlog file path, level=1-6\n"
           "    --accesslog=path \t\taccess log file path\n"
           "    --host-route=path \t\tSpecial route file path\n"
           "    --app=path \t\t\tSpecial app file path\n"
           "    --code-cache-ttl \t\tnumber of code cache time(sec) default 60 sec\n"
           "    --cache-size \t\tsize of YAC shared memory cache (1m or 4096000k)\n"
           "  \n"
           "--------------------------------------------------------------------------------\n",
           "aLiLua", ALILUA_VERSION, program_name, bind_port, bind_port
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
        LOGF(ERR, "error for luaL_newstate");
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

    if(getarg("host-route")) {
        lua_pushstring(_L, getarg("host-route"));
        lua_setglobal(_L, "HOST_ROUTE");
    }

    if(!update_vhost_routes(getarg("host-route")) && !getarg("app")) {
        LOGF(WARN, "no host-route or app arguments! using defalut settings.");
        sprintf(tbuf_4096, "%s/host-route.lua", process_chdir);
        update_vhost_routes(tbuf_4096);
    }

    if(getarg("code-cache-ttl")) {       /// default = 60s
        lua_pushnumber(_L, atoi(getarg("code-cache-ttl")));
        lua_setglobal(_L, "CODE_CACHE_TTL");

    } else {
        lua_pushnumber(_L, code_cache_ttl);
        lua_setglobal(_L, "CODE_CACHE_TTL");
    }

    lua_getglobal(_L, "require");
    lua_pushcfunction(_L, lua_f_package_require);
    lua_getfenv(_L, -2);
    int ret = lua_setfenv(_L, -2);
    lua_setglobal(_L, "require");
    lua_pop(_L, 1);

    lua_register(_L, "echo", lua_echo);
    lua_register(_L, "print_error", lua_print_error);
    lua_register(_L, "sendfile", lua_sendfile);
    lua_register(_L, "header", lua_header);
    lua_register(_L, "clear_header", lua_clear_header);
    lua_register(_L, "__end", lua_end);
    lua_register(_L, "die", lua_die);
    lua_register(_L, "flush", lua_flush);
    lua_register(_L, "read_request_body", lua_read_request_body);
    lua_register(_L, "get_boundary", lua_f_get_boundary);
    lua_register(_L, "check_timeout", lua_check_timeout);
    lua_register(_L, "is_websocket", lua_f_is_websocket);
    lua_register(_L, "upgrade_to_websocket", lua_f_upgrade_to_websocket);
    lua_register(_L, "websocket_send", lua_f_websocket_send);
    lua_register(_L, "check_websocket_close", lua_f_check_websocket_close);

    lua_register(_L, "router", lua_f_router);

    lua_register(_L, "random_string", lua_f_random_string);
    lua_register(_L, "file_exists", lua_f_file_exists);
    lua_register(_L, "readfile", lua_f_readfile);
    lua_register(_L, "filemtime", lua_f_filemtime);

    lua_register(_L, "cache_set", lua_f_cache_set);
    lua_register(_L, "cache_get", lua_f_cache_get);
    lua_register(_L, "cache_del", lua_f_cache_del);

    luaopen_fastlz(_L);
    luaopen_coevent(_L);
    luaopen_libfs(_L);
    luaopen_string_utils(_L);
    luaopen_i18n(_L);
    luaopen_crypto(_L);

    lua_pop(_L, 1);

    sprintf(tbuf_4096,
            "package.path = '%slua-libs/?.lua;' .. package.path package.cpath = '%slua-libs/?.so;' .. package.cpath", cwd, cwd);
    luaL_dostring(_L, tbuf_4096);

    luaL_dostring(_L, ""
                  "if not CODE_CACHE_TTL then CODE_CACHE_TTL = 60 end " \
                  "startloop = nil __CodeCache = {{},{}} __CodeCacheC = {false,false} "
                 );

    if(getarg("accesslog")) {
        ACCESS_LOG = open_log(getarg("accesslog"), 40960);

        if(!ACCESS_LOG) {
            LOGF(ERR, "Couldn't open access log file: %s", getarg("accesslog"));
        }
    }

    if(getarg("ssl-bind") && getarg("ssl-cert") && getarg("ssl-key")) {
        ssl_ctx = SSL_CTX_new(SSLv23_server_method());

        if(!ssl_ctx) {
            LOGF(ERR, "SSL_CTX_new Failed");
            exit(1);
        }

        if(SSL_CTX_use_certificate_file(ssl_ctx, getarg("ssl-cert"), SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(ssl_ctx);
            LOGF(ERR, "SSL_CTX_use_certificate_file");
            exit(1);
        }

        if(SSL_CTX_use_PrivateKey_file(ssl_ctx, getarg("ssl-key"), SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(ssl_ctx);
            LOGF(ERR, "SSL_CTX_use_PrivateKey_file");
            exit(1);
        }

        SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);

        if(getarg("ssl-ca")) {
            ssl_epd_idx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);

            if(ssl_epd_idx == -1) {
                LOGF(ERR, "SSL_get_ex_new_index Failed");
                exit(1);
            }

            if(SSL_CTX_load_verify_locations(ssl_ctx, getarg("ssl-ca"), NULL) != 1) {
                SSL_CTX_free(ssl_ctx);
                LOGF(ERR, "SSL_CTX_load_verify_locations");
                exit(1);

            } else {
                SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
                SSL_CTX_set_verify_depth(ssl_ctx, 4);

            }
        }
    }

    _shm_serv_status = shm_malloc(sizeof(serv_status_t));
    bzero(_shm_serv_status->p, sizeof(serv_status_t));

    attach_on_exit(on_master_exit_handler);
    return merry_start(argc, argv, help, master_main, on_master_exit_handler, worker_main, 0);
}
