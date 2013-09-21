#include "main.h"

int server_fd = 0;
FILE *LOG_FD = NULL;
static char LOG_BUF[40960];
FILE *ERR_FD = NULL;
static char ERR_BUF[4096];

static lua_State *_L;
static int process_count = 2;
static char tbuf_4096[4096];
static int working_at_fd = 0;

static int main_handle_ref = 0;

static void *process_checker()
{
    while ( 1 ) {
        if ( !checkProcessForExit() ) {
            safeProcess();
        }

        sleep ( 1 );
    }
}

static void on_exit_handler ( int sig, siginfo_t *info, void *secret )
{
    /*
        epdata_t *epd, *next;
        int i,j;
        for(i=0;i<60;i++){
            for(j=0;j<1024;j++){
                next = epd_pool[i][j];
                while(next != NULL){
                    if(next){
                        epd = next;
                        next = epd->next;
                        if(working_at_fd != epd->fd)network_send_error(epd, 500, "Child Process restart!");
                        else network_send_error(epd, 410, "Child Process Segmentation fault!");
                        close_client(epd);
                    }
                }
            }
        }*/

    exit ( 0 );
}

static void on_master_exit_handler ( int sig, siginfo_t *info, void *secret )
{
    waitForChildProcessExit();

    if ( LOG_FD ) {
        fflush ( LOG_FD );
        fclose ( LOG_FD );
    }

    if ( ERR_FD ) {
        fflush ( ERR_FD );
        fclose ( ERR_FD );
    }
}

static void master_main()
{
    setProcTitle ( "master process", 1 );

    _process_chdir = process_chdir;

    if ( !new_thread ( process_checker ) ) {
        perror ( "process checker thread error!" );
        exit ( -1 );
    }

    int i = 0;

    while ( 1 ) {
        if ( checkProcessForExit() ) {
            kill ( 0, SIGTERM ); /// 关闭子进程
            on_master_exit_handler ( 0, NULL, NULL );
            exit ( 0 );
        }

        sleep ( 1 );
        i++;
    }
}

int worker_process ( epdata_t *epd, int thread_at )
{
    //printf("worker_process\n");
    working_at_fd = epd->fd;
    long start_time = longtime();
    //network_send_error(epd, 503, "Lua Error: main function not found !!!");return 0;
    //network_send(epd, "aaa", 3);network_be_end(epd);return 0;
    add_io_counts();
    lua_State *L = ( _L ); //lua_newthread

    if ( main_handle_ref != 0 ) {
        lua_rawgeti ( L, LUA_REGISTRYINDEX, main_handle_ref );

    } else {
        lua_getglobal ( L, "main" );
    }

    /*if(!lua_isfunction(L,-1))
    {
        lua_pop(L,1);
        printf("no function\n");
    }else*/{
        int init_tables = 0;
        char *pt1 = NULL, *pt2 = NULL, *t1 = NULL, *t2 = NULL, *t3 = NULL, *query = NULL;

        int is_form_post = 0;
        char *boundary_post = NULL;
        char *cookies = NULL;
        pt1 = epd->headers;
        int i = 0;

        while ( t1 = strtok_r ( pt1, "\n", &pt1 ) ) {
            if ( ++i == 1 ) { /// first line
                t2 = strtok_r ( t1, " ", &t1 );
                t3 = strtok_r ( t1, " ", &t1 );
                epd->http_ver = strtok_r ( t1, " ", &t1 );

                if ( !epd->http_ver ) {
                    return 1;

                } else {
                    if ( init_tables == 0 ) {
                        lua_pushlightuserdata ( L, epd );
                        //lua_pushstring(L, epd->headers);
                        lua_newtable ( L );
                    }
                }

                int len = strlen ( epd->http_ver );

                if ( epd->http_ver[len - 1] == 13 ) { // CR == 13
                    epd->http_ver[len - 1] = '\0';
                }

                if ( t2 && t3 ) {
                    for ( t1 = t2 ; *t1 ; *t1 = toupper ( *t1 ), t1++ );

                    epd->method = t2;
                    lua_pushstring ( L, t2 );
                    lua_setfield ( L, -2, "method" );
                    t1 = strtok_r ( t3, "?", &t3 );
                    t2 = strtok_r ( t3, "?", &t3 );
                    epd->uri = t1;
                    lua_pushstring ( L, t1 );
                    lua_setfield ( L, -2, "uri" );

                    if ( t2 ) {
                        epd->query = t2;
                        query = t2;
                        lua_pushstring ( L, t2 );
                        lua_setfield ( L, -2, "query" );
                    }
                }

                continue;
            }

            t2 = strtok_r ( t1, ":", &t1 );

            if ( t2 ) {
                for ( t3 = t2; *t3; ++t3 ) {
                    *t3 = *t3 >= 'A' && *t3 <= 'Z' ? *t3 | 0x60 : *t3;
                }

                t3 = strtok_r ( t1, ":", &t1 );

                if ( t3 ) {
                    int len = strlen ( t3 );

                    if ( t3[len - 1] == 13 ) { /// 13 == CR
                        t3[len - 1] = '\0';
                    }

                    lua_pushstring ( L, t3 + ( t3[0] == ' ' ? 1 : 0 ) );
                    lua_setfield ( L, -2, t2 );

                    /// check content-type
                    if ( t2[1] == 'o' && strcmp ( t2, "content-type" ) == 0 ) {
                        if ( stristr ( t3, "x-www-form-urlencoded", len ) ) {
                            is_form_post = 1;

                        } else if ( stristr ( t3, "multipart/form-data", len ) ) {
                            boundary_post = stristr ( t3, "boundary=", len - 2 );
                        }

                    } else if ( !cookies && t2[1] == 'o' && strcmp ( t2, "cookie" ) == 0 ) {
                        cookies = t3 + ( t3[0] == ' ' ? 1 : 0 );

                    } else if ( !epd->user_agent && t2[1] == 's' && strcmp ( t2, "user-agent" ) == 0 ) {
                        epd->user_agent = t3 + ( t3[0] == ' ' ? 1 : 0 );

                    } else if ( !epd->referer && t2[1] == 'e' && strcmp ( t2, "referer" ) == 0 ) {
                        epd->referer = t3 + ( t3[0] == ' ' ? 1 : 0 );
                    }
                }
            }
        }

        char *client_ip = inet_ntoa ( epd->client_addr );
        lua_pushstring ( L, client_ip );
        lua_setfield ( L, -2, "remote-addr" );

        lua_newtable ( L ); /// _GET

        if ( query ) { /// parse query string /?a=1&b=2
            while ( t1 = strtok_r ( query, "&", &query ) ) {
                t2 = strtok_r ( t1, "=", &t1 );
                t3 = strtok_r ( t1, "=", &t1 );

                if ( t2 && t3 && strlen ( t2 ) > 0 && strlen ( t3 ) > 0 ) {
                    size_t len, dlen;
                    u_char *p;
                    u_char *src, *dst;
                    len = strlen ( t3 );
                    p = large_malloc ( len );
                    p[0] = '\0';
                    dst = p;
                    ngx_http_lua_unescape_uri ( &dst, &t3, len, 0 );
                    lua_pushlstring ( L, ( char * ) p, dst - p );

                    len = strlen ( t2 );

                    if ( len > 4096 ) {
                        free ( p );
                        p = large_malloc ( len );
                    }

                    p[0] = '\0';
                    dst = p;

                    ngx_http_lua_unescape_uri ( &dst, &t2, len, 0 );
                    p[dst - p] = '\0';
                    lua_setfield ( L, -2, p );
                    free ( p );
                }
            }
        }

        lua_newtable ( L ); /// _COOKIE

        if ( cookies ) {
            while ( t1 = strtok_r ( cookies, ";", &cookies ) ) {
                t2 = strtok_r ( t1, "=", &t1 );
                t3 = strtok_r ( t1, "=", &t1 );

                if ( t2 && t3 && strlen ( t2 ) > 0 && strlen ( t3 ) > 0 ) {
                    size_t len, dlen;
                    u_char *p;
                    u_char *src, *dst;
                    len = strlen ( t3 );
                    p = large_malloc ( len );
                    p[0] = '\0';
                    dst = p;
                    ngx_http_lua_unescape_uri ( &dst, &t3, len, 0 );
                    lua_pushlstring ( L, ( char * ) p, dst - p );

                    len = strlen ( t2 );

                    if ( len > 4096 ) {
                        free ( p );
                        p = large_malloc ( len );
                    }

                    p[0] = '\0';
                    dst = p;

                    ngx_http_lua_unescape_uri ( &dst, &t2, len, 0 );
                    p[dst - p] = '\0';
                    lua_setfield ( L, -2, p + ( p[0] == ' ' ? 1 : 0 ) );
                    free ( p );
                }
            }
        }

        lua_newtable ( L ); /// _POST

        if ( is_form_post == 1
             && epd->contents ) { /// parse post conents text=aa+bb&text2=%E4%B8%AD%E6%96%87+aa
            pt1 = epd->contents;

            while ( t1 = strtok_r ( pt1, "&", &pt1 ) ) {
                t2 = strtok_r ( t1, "=", &t1 );
                t3 = strtok_r ( t1, "=", &t1 );

                if ( t2 && t3 && strlen ( t2 ) > 0 && strlen ( t3 ) > 0 ) {
                    size_t len, dlen;
                    u_char *p;
                    u_char *src, *dst;
                    len = strlen ( t3 );
                    p = large_malloc ( len );
                    p[0] = '\0';
                    dst = p;
                    ngx_http_lua_unescape_uri ( &dst, &t3, len, 0 );
                    lua_pushlstring ( L, ( char * ) p, dst - p );
                    free ( p );
                    //lua_pushstring(L, t3);
                    lua_setfield ( L, -2, t2 );
                }
            }

        } else if ( boundary_post ) { /// parse boundary body
            boundary_post += 9;
            int blen = strlen ( boundary_post );
            int len = 0;
            char *start = epd->contents, *p2 = NULL, *p1 = NULL, *pp = NULL, *value = NULL;
            int i = 0;

            do {
                p2 = strstr ( start, boundary_post );

                if ( p2 ) {
                    start = p2 + blen;
                }

                if ( p1 ) {
                    p1 += blen;

                    if ( p2 ) {
                        * ( p2 - 4 ) = '\0';

                    } else {
                        break;
                    }

                    len = p2 - p1;
                    value = stristr ( p1, "\r\n\r\n", len );

                    if ( value && value[4] != '\0' ) {
                        value[0] = '\0';
                        value += 4;
                        char *keyname = strstr ( p1, "name=\"" );
                        char *filename = NULL;
                        char *content_type = NULL;

                        if ( keyname ) {
                            keyname += 6;

                            for ( pp = keyname; *pp != '\0'; pp++ ) {
                                if ( *pp == '"' ) {
                                    *pp = '\0';
                                    p1 = pp + 2;
                                    break;
                                }
                            }

                            filename = strstr ( p1, "filename=\"" );

                            if ( filename ) { /// is file filed
                                filename += 10;

                                for ( pp = filename; *pp != '\0'; pp++ ) {
                                    if ( *pp == '"' ) {
                                        *pp = '\0';
                                        p1 = pp + 2;
                                        break;
                                    }
                                }

                                content_type = strstr ( p1, "Content-Type:" );

                                if ( content_type ) {
                                    content_type += 13;

                                    if ( content_type[0] == ' ' ) {
                                        content_type += 1;
                                    }
                                }

                                lua_newtable ( L );
                                lua_pushstring ( L, filename );
                                lua_setfield ( L, -2, "filename" );
                                lua_pushstring ( L, content_type );
                                lua_setfield ( L, -2, "type" );
                                lua_pushnumber ( L, p2 - value - 4 );
                                lua_setfield ( L, -2, "size" );
                                lua_pushlstring ( L, value, p2 - value - 4 );
                                lua_setfield ( L, -2, "data" );

                                lua_setfield ( L, -2, keyname );

                            } else {
                                lua_pushstring ( L, value );
                                lua_setfield ( L, -2, keyname );
                            }
                        }
                    }
                }

                p1 = p2 + 2;
            } while ( p2 );

            free ( epd->headers );
            epd->headers = NULL;
            epd->header_len = 0;
            epd->contents = NULL;
            epd->content_length = 0;
        }

        if ( lua_pcall ( L, 5, 0, 0 ) ) {
            if ( lua_isstring ( L, -1 ) ) {
                printf ( "Lua:error %s\n", lua_tostring ( L, -1 ) );
                const char *data = lua_tostring ( L, -1 );
                network_send_error ( epd, 503, data );
                lua_pop ( L, 1 );
                //network_be_end(epd);
            }
        }

        return 0;
    }

    network_send_error ( epd, 503, "Lua Error: main function not found !!!" );

    return 0;
}

static void worker_main ( int at )
{

    attach_on_exit ( on_exit_handler );

    setProcessUser ( /*user*/ NULL, /*group*/ NULL );

    setProcTitle ( "worker process", 0 );
    _process_chdir = process_chdir;

    /// 进入 loop 处理循环
    network_worker ( worker_process, process_count, at );

    exit ( 0 );
}

int main ( int argc, char *argv[] )
{
    attach_on_exit ( on_master_exit_handler );

    /// 初始化进程命令行信息
    char *cwd = initProcTitle ( argc, argv );

    char *msg = NULL;

    if ( !yac_storage_startup ( YAC_KEY_DATA_SIZE, YAC_VALUE_DATA_SIZE, &msg ) ) {
        printf ( "Shared memory allocator startup failed at '%s': %s", msg,
                 strerror ( errno ) );
        exit ( 1 );
    }

    if ( getarg ( "log" ) && strlen ( getarg ( "log" ) ) > 0 ) {
        LOG_FD = fopen ( getarg ( "log" ), "a+" );

        if ( !LOG_FD ) {
            printf ( "fopen %s error\n" , getarg ( "log" ) );
            return -1;
        }

        setvbuf ( LOG_FD , LOG_BUF, _IOFBF , 40960 );
    }

    if ( getarg ( "errorlog" ) && strlen ( getarg ( "errorlog" ) ) > 0 ) {
        ERR_FD = fopen ( getarg ( "errorlog" ), "a+" );

        if ( !ERR_FD ) {
            printf ( "fopen %s error\n" , getarg ( "errorlog" ) );
            return -1;
        }

        setvbuf ( ERR_FD , ERR_BUF, _IOFBF , 4096 );
    }

    hostname[1023] = '\0';
    gethostname ( hostname, 1000 );

    lua_State *L = luaL_newstate();
    luaL_openlibs ( L );
    lua_getglobal ( L, "_VERSION" );
    const char *lua_ver = lua_tostring ( L, -1 );
    lua_getglobal ( L, "jit" );

    if ( lua_istable ( L, -1 ) ) {
        lua_getfield ( L, -1, "version" );

        if ( lua_isstring ( L, -1 ) ) {
            lua_ver = lua_tostring ( L, -1 );
        }
    }

    sprintf ( hostname, "%s/%s", hostname, lua_ver );
    lua_close ( L );

    if ( getarg ( "help" ) ) {
        printf ( "This is the aLiLua/%s Web Server.  Usage:\n"
                 "\n"
                 "    alilua [options]\n"
                 "\n"
                 "Options:\n"
                 "\n"
                 "  --bind=127.0.0.1:80  server bind. or --bind=80 for bind at 0.0.0.0:80\n"
                 "  --daemon             process mode\n"
                 "  --process=number     workers\n"
                 "  --log=file-path      access log\n"
                 "  --errorlog=file-path error log\n"
                 "  --host-route         Special route file path\n"
                 "  --code-cache-ttl     number of code cache time(sec)\n"
                 "  \n"
                 "\n",
                 version
               );
        exit ( 0 );
    }

    /// 把进程放入后台
    if ( getarg ( "daemon" ) ) {
        is_daemon = 1;
        daemonize();
    }

    /// 创建4个子进程
    if ( getarg ( "process" ) ) {
        process_count = atoi ( getarg ( "process" ) );

    } else if ( process_count > 32 ) {
        process_count = 32;
    }

    if ( !getarg ( "daemon" ) ) {
        process_count = 1;
    }

    char *bind_addr = "0.0.0.0";

    if ( getarg ( "bind" ) ) {
        bind_addr = getarg ( "bind" );
    }

    char *_port;

    if ( !strstr ( bind_addr, "." ) ) {
        bind_addr = "0.0.0.0";
        _port = getarg ( "bind" );

    } else {
        _port = strstr ( bind_addr, ":" );

        if ( _port ) {
            bind_addr[strlen ( bind_addr ) - strlen ( _port )] = '\0';
            _port = _port + 1;
        }
    }

    int port = default_port;

    if ( _port ) {
        port = atoi ( _port );
    }

    if ( port < 1 ) {
        port = default_port;
    }

    int i = 0, status = 0;
    {
        /// init lua state
        _L = luaL_newstate();
        lua_gc ( _L, LUA_GCSTOP, 0 );
        luaL_openlibs ( _L ); /* Load Lua libraries */
        lua_gc ( _L, LUA_GCRESTART, 0 );

        if ( getarg ( "code-cache-ttl" ) ) { /// default = 60s
            lua_pushnumber ( _L, atoi ( getarg ( "code-cache-ttl" ) ) );
            lua_setglobal ( _L, "CODE_CACHE_TTL" );
        }

        if ( getarg ( "host-route" ) ) {
            lua_pushstring ( _L, getarg ( "host-route" ) );
            lua_setglobal ( _L, "HOST_ROUTE" );
        }

        lua_register ( _L, "errorlog", lua_errorlog );
        lua_register ( _L, "echo", lua_echo );
        lua_register ( _L, "sendfile", lua_sendfile );
        lua_register ( _L, "header", lua_header );
        lua_register ( _L, "clear_header", lua_clear_header );
        lua_register ( _L, "die", lua_die );
        lua_register ( _L, "get_post_body", lua_get_post_body );
        lua_register ( _L, "check_timeout", lua_check_timeout );

        lua_register ( _L, "random_string", lua_f_random_string );
        lua_register ( _L, "file_exists", lua_f_file_exists );

        lua_register ( _L, "cache_set", lua_f_cache_set );
        lua_register ( _L, "cache_get", lua_f_cache_get );
        lua_register ( _L, "cache_del", lua_f_cache_del );

        luaopen_fastlz ( _L );
        luaopen_coevent ( _L );
        luaopen_libfs ( _L );
        luaopen_string_utils ( _L );
        luaopen_crypto ( _L );

        lua_pop ( _L, 1 );

        sprintf ( tbuf_4096,
                  "package.path = '%s/lua-libs/?.lua;' .. package.path package.cpath = '%s/lua-libs/?.so;' .. package.cpath",
                  cwd, cwd );
        luaL_dostring ( _L, tbuf_4096 );

        /* Load the file containing the script we are going to run */
        status = luaL_loadfile ( _L, "script.lua" );

        if ( status || lua_resume ( _L, 0 ) ) {
            /* If something went wrong, error message is at the top of */
            /* the stack */
            fprintf ( stderr, "Couldn't load file: %s\n", lua_tostring ( _L, -1 ) );
            exit ( 1 );
        }

        lua_getglobal ( _L, "main" );
        main_handle_ref = luaL_ref ( _L, LUA_REGISTRYINDEX );
    }

    server_fd = network_bind ( bind_addr, port );

    for ( i = 0; i < process_count; i++ ) {
        if ( getarg ( "daemon" ) ) {
            forkProcess ( worker_main );

        } else {
            active_cpu ( 0 );
            new_thread_i ( worker_main, 0 );
        }
    }

    /// 设置进程归属用户
    setProcessUser ( /*user*/ NULL, /*group*/ NULL );

    /// 进入主进程处理
    master_main();

    return 0;
}
