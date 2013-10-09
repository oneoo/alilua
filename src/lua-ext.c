#include "main.h"

static char tbuf_4096[4096];

extern FILE *ERR_FD;
int lua_errorlog ( lua_State *L )
{
    if ( !ERR_FD ) {
        return 0;
    }

    char now_date2[100];
    time_t now2;
    time ( &now2 );
    static struct tm now_tm2;
    gmtime_r ( &now2, &now_tm2 );
    sprintf ( now_date2, "%s, %02d %s %04d %02d:%02d:%02d GMT",
              DAYS_OF_WEEK[now_tm2.tm_wday],
              now_tm2.tm_mday,
              MONTHS_OF_YEAR[now_tm2.tm_mon],
              now_tm2.tm_year + 1900,
              now_tm2.tm_hour,
              now_tm2.tm_min,
              now_tm2.tm_sec );

    if ( lua_isuserdata ( L, 1 ) ) {
        epdata_t *epd = lua_touserdata ( L, 1 );
        fprintf ( ERR_FD,
                  "[%s] %s %s \"%s %s%s%s %s\" \"%s\" \"%s\" {%s}\n",
                  now_date2,
                  inet_ntoa ( epd->client_addr ),
                  epd->host ? epd->host : "-",
                  epd->method ? epd->method : "-",
                  epd->uri ? epd->uri : "/",
                  epd->query ? "?" : "",
                  epd->query ? epd->query : "",
                  epd->http_ver ? epd->http_ver : "-",
                  epd->referer ? epd->referer : "-",
                  epd->user_agent ? epd->user_agent : "-",
                  lua_tostring ( L, 2 ) );

    } else if ( lua_isstring ( L, 1 ) ) {
        fprintf ( ERR_FD,
                  "[%s] miss connection infos {%s}\n",
                  now_date2,
                  lua_tostring ( L, 1 ) );

    } else if ( lua_isstring ( L, 2 ) ) {
        fprintf ( ERR_FD,
                  "[%s] miss connection infos {%s}\n",
                  now_date2,
                  lua_tostring ( L, 2 ) );
    }

    return 0;
}

typedef struct {
    void *L;
    time_t timeout;
    void *uper;
    void *next;
} sleep_timeout_t;
#define NULL8 NULL, NULL, NULL, NULL,NULL, NULL, NULL, NULL,
static sleep_timeout_t *timeout_links[64] = {NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8};
static sleep_timeout_t *timeout_link_ends[64] = {NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8};
static time_t now_4sleep;
int check_lua_sleep_timeouts()
{
    time ( &now_4sleep );
    int k = now_4sleep % 64;

    sleep_timeout_t *m = timeout_links[k], *n = NULL;

    while ( m ) {
        n = m;
        m = m->next;

        if ( now >= n->timeout ) { // timeout
            lua_resume ( n->L, 0 );

            //delete_timeout ( n );
            {
                if ( n->uper ) {
                    ( ( sleep_timeout_t * ) n->uper )->next = n->next;

                } else {
                    timeout_links[k] = n->next;
                }

                if ( n->next ) {
                    ( ( sleep_timeout_t * ) n->next )->uper = n->uper;

                } else {
                    timeout_link_ends[k] = n->uper;
                }

                free ( n );
            }
        }
    }

    return 1;
}

int lua_f_sleep ( lua_State *L )
{
    if ( !lua_isnumber ( L, 1 ) ) {
        return 0;
    }

    int sec = lua_tonumber ( L, 1 );

    if ( sec < 1 ) {
        return 0;
    }

    if ( sec > 1000000 ) {
        return lua_yield ( L, 0 );    // for ever
    }

    time ( &now_4sleep );

    sleep_timeout_t *n = malloc ( sizeof ( sleep_timeout_t ) );

    if ( !n ) {
        return 0;
    }

    n->timeout = now_4sleep + sec;
    n->uper = NULL;
    n->next = NULL;
    n->L = L;

    int k = n->timeout % 64;

    if ( timeout_link_ends[k] == NULL ) {
        timeout_links[k] = n;
        timeout_link_ends[k] = n;

    } else { // add to link end
        timeout_link_ends[k]->next = n;
        n->uper = timeout_link_ends[k];
        timeout_link_ends[k] = n;
    }

    return lua_yield ( L, 0 );
}

int lua_check_timeout ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    if ( epd->process_timeout == 1 ) {
        epd->keepalive = 0;
        network_be_end ( epd );
        lua_pushstring ( L, "Process Time Out!" );
        lua_error ( L ); /// stop lua script
    }

    return 0;
}

int lua_header ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    if ( lua_gettop ( L ) < 2 ) {
        return 0;
    }

    int t = lua_type ( L, 2 );
    size_t dlen = 0;
    const char *data = NULL;

    if ( t == LUA_TSTRING ) {
        data = lua_tolstring ( L, 2, &dlen );

        if ( stristr ( data, "content-length", dlen ) != data ) {
            network_send_header ( epd, data );
        }

    } else if ( t == LUA_TTABLE ) {
        int len = lua_objlen ( L, 2 ), i = 0;

        for ( i = 0; i < len; i++ ) {
            lua_pushinteger ( L, i + 1 );
            lua_gettable ( L, -2 );

            if ( lua_isstring ( L, -1 ) ) {
                data = lua_tolstring ( L, -1, &dlen );

                if ( stristr ( data, "content-length", dlen ) != data ) {
                    network_send_header ( epd, lua_tostring ( L, -1 ) );
                }
            }

            lua_pop ( L, 1 );
        }
    }

    return 0;
}

int lua_echo ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    int nargs = lua_gettop ( L );

    if ( nargs < 2 ) {
        luaL_error ( L, "miss content!" );
        return 0;
    }

    size_t len = 0;
    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    if ( lua_istable ( L, 2 ) ) {
        len = lua_calc_strlen_in_table ( L, 2, 2, 0 /* strict */ );

        if ( len < 1 ) {
            return 0;
        }

        char *buf = tbuf_4096;

        if ( len > 4096 ) {
            buf = large_malloc ( len );

            if ( !buf ) {
                return 0;
            }

            lua_copy_str_in_table ( L, 2, buf );
            network_send ( epd, buf, len );
            free ( buf );

        } else {
            lua_copy_str_in_table ( L, 2, buf );
            network_send ( epd, buf, len );
        }

    } else {
        const char *data = NULL;
        int i = 0;

        for ( i = 2; i <= nargs; i++ ) {
            if ( lua_isboolean ( L, i ) ) {
                if ( lua_toboolean ( L, i ) ) {
                    network_send ( epd, "true", 4 );

                } else {
                    network_send ( epd, "false", 5 );
                }

            } else {
                data = lua_tolstring ( L, i, &len );
                network_send ( epd, data, len );
            }
        }
    }

    return 0;
}

int lua_clear_header ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );
    epd->response_header_length = 0;
    return 0;
}

int lua_sendfile ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    if ( !lua_isstring ( L, 2 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Need a file path!" );
        return 2;
    }

    network_sendfile ( epd, lua_tostring ( L, 2 ) );

    return 0;
}

int lua_die ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    network_be_end ( epd );
    lua_pushnil ( L );
    lua_error ( L ); /// stop lua script
    return 0;
}

int lua_get_post_body ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    if ( epd->content_length > 0 ) {
        lua_pushlstring ( L, epd->contents, epd->content_length );

    } else {
        lua_pushnil ( L );
    }

    free ( epd->headers );
    epd->headers = NULL;
    epd->header_len = 0;
    epd->contents = NULL;
    epd->content_length = 0;

    return 1;
}

int lua_f_random_string ( lua_State *L )
{
    int size = 32;

    if ( lua_gettop ( L ) == 1 && lua_isnumber ( L, 1 ) ) {
        size = lua_tonumber ( L, 1 );
    }

    if ( size < 1 ) {
        size = 32;
    }

    if ( size > 4096 ) {
        return 0;
    }

    random_string ( tbuf_4096, size, 0 );

    lua_pushlstring ( L, tbuf_4096, size );

    return 1;
}

int lua_f_file_exists ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Need a file path!" );
        return 2;
    }

    const char *fname = lua_tostring ( L, 1 );

    lua_pushboolean ( L, access ( fname, F_OK ) != -1 );

    return 1;
}

int lua_f_is_websocket ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    lua_pushboolean ( L, epd->websocket ? 1 : 0 );
    return 1;
}
/*
$ sysctl net.inet.tcp.always_keepalive
net.inet.tcp.always_keepalive: 0
$ sudo sysctl -w net.inet.tcp.always_keepalive=1
net.inet.tcp.always_keepalive: 0 -> 1
*/
int lua_f_upgrade_to_websocket ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    luaL_checktype ( L, 2, LUA_TTABLE );
    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( epd->websocket ) {
        return 0;
    }

    epd->keepalive = 1;
    epd->websocket = malloc ( sizeof ( websocket_pt ) );

    if ( !epd->websocket ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    network_be_end ( epd );
    //lua_pushnil ( L );
    epd->websocket->ML = L;
    epd->websocket->L = NULL;
    epd->websocket->data = NULL;

    lua_pushvalue ( L, 2 );
    epd->websocket->websocket_handles = luaL_ref ( L, LUA_REGISTRYINDEX );
    //printf ( "websocket_handles %d\n", epd->websocket->websocket_handles );
    return 0;//yield at lua script
    //return lua_yield ( L, 0 );
}


int lua_f_websocket_send ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        luaL_error ( L, "miss epd!" );
        return 0;
    }

    epdata_t *epd = lua_touserdata ( L, 1 );

    if ( !epd->websocket ) {
        luaL_error ( L, "not a websocket!" );
        return 0;
    }

    if ( epd->websocket->data ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "websocket busy!" );
        return 2;
    }

    if ( !lua_isstring ( L, 2 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "miss content!" );
        return 0;
    }

    size_t len;
    const char *data = lua_tolstring ( L, 2, &len );
    epd->websocket->L = L;
    int r = ws_send_data ( epd, 1, 0, 0, 0, WS_OPCODE_TEXT, len, data );

    if ( r == -1 ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "send error!" );
        return 2;
    }

    if ( r == 1 ) {
        epd->websocket->L = L; // for lua_State ptr
        return lua_yield ( L, 0 );
    }

    lua_pushboolean ( L, 1 );
    return 1;
}