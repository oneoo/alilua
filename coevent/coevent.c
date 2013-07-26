#include "coevent.h"
#include "connection-pool.h"

static int epoll_fd = 0;
static lua_State *LM = NULL;
static int clearthreads_handler = 0;
static unsigned char temp_buf[4096];
static int process_count = 1;
static int io_counts = 0;

int lua_co_resume ( lua_State *L , int nargs )
{
    int ret = lua_resume ( L, nargs );

    if ( ret == LUA_ERRRUN && lua_isstring ( L, -1 ) ) {
        printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( L, -1 ) );

        //lua_pop(cok->L, -1);
        if ( lua_gettop ( L ) > 1 ) {
            lua_replace ( L, 2 );
            lua_pushnil ( L );
            lua_replace ( L, 1 );
            lua_settop ( L, 2 );

        } else {
            lua_pushnil ( L );
            lua_replace ( L, 1 );
        }

        lua_f_coroutine_resume_waiting ( L );

    } else {
        ret = 0;
    }

    return ret;
}

int cosocket_be_connected ( se_ptr_t *ptr )
{
    cosocket_t *cok = ptr->data;

    int result = 0;
    socklen_t result_len = sizeof ( result );

    if ( getsockopt ( cok->fd, SOL_SOCKET, SO_ERROR, &result, &result_len ) < 0 ) {
        /// not connected, try next event
        return 0;
    }

    /*if(!del_in_timeout_link(cok)){
        printf("del error %d  fd %d\n", __LINE__, cok->fd);
        exit(1);
    }*/
    del_in_timeout_link ( cok );

    if ( result != 0 ) { /// connect error
        {
            se_delete ( cok->ptr );
            cok->ptr = NULL;
//printf("0x%x close fd %d   l:%d\n", cok->L, cok->fd, __LINE__);
            connection_pool_counter_operate ( cok->pool_key, -1 );
            close ( cok->fd );
            cok->fd = -1;
            cok->status = 0;
        }

        lua_pushnil ( cok->L );
        lua_pushstring ( cok->L, "Connect error!(2)" );
        cok->inuse = 0;

        lua_co_resume ( cok->L, 2 );

    } else { /// connected
        cok->status = 2;
        cok->in_read_action = 0;
        se_be_pri ( cok->ptr, NULL );
        lua_pushboolean ( cok->L, 1 );
        cok->inuse = 0;
        lua_co_resume ( cok->L, 1 );
    }
}

static int lua_co_connect ( lua_State *L )
{
    cosocket_t *cok = NULL;
    {
        if ( !lua_isuserdata ( L, 1 ) || !lua_isstring ( L, 2 ) ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Error params!" );
            return 2;
        }

        cok = ( cosocket_t * ) lua_touserdata ( L, 1 );

        if ( cok->status > 0 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Aleady connected!" );
            return 2;
        }

        if ( cok->inuse == 1 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "socket busy!" );
            return 2;
        }

//printf(" 0x%x connect to %s\n", L, lua_tostring(L, 2));
        size_t host_len = 0;
        const char *host = lua_tolstring ( L, 2, &host_len );

        if ( host_len > ( host[0] == '/' ? 108 : 60 ) ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "hostname length must be <= 60!" );
            return 2;
        }

        int port = 0;
        int pn = 3;

        if ( host[0] != '/' ) {
            port = lua_tonumber ( L, 3 );

            if ( port < 1 ) {
                lua_pushnil ( L );
                lua_pushstring ( L, "port must be > 0" );
                return 2;
            }

            pn = 4;
        }

        if ( lua_gettop ( L ) >= pn ) { /// set keepalive options
            if ( lua_isnumber ( L, pn ) ) {
                cok->pool_size = lua_tonumber ( L, pn );

                if ( cok->pool_size < 0 || cok->pool_size > 1000 ) {
                    cok->pool_size = 0;
                }

                pn++;
            }

            if ( cok->pool_size > 0 ) {
                size_t len = 0;

                if ( lua_gettop ( L ) == pn && lua_isstring ( L, pn ) ) {
                    const char *key = lua_tolstring ( L, pn, &len );
                    cok->pool_key = fnv1a_32 ( key, len );
                }
            }
        }

        if ( cok->pool_key == 0 ) { /// create a normal key
            int len = sprintf ( temp_buf, "%s%s:%d", port > 0 ? "tcp://" : "unix://", host, port );
            cok->pool_key = fnv1a_32 ( temp_buf, len );
        }

        cok->status = 1;
        cok->L = L;
        cok->read_buf = NULL;
        cok->last_buf = NULL;
        cok->total_buf_len = 0;
        cok->buf_read_len = 0;

        /// check pool count
        if ( cok->pool_size > 0 ) {
            cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter (
                        cok->pool_key );

            if ( pool_counter->count >= cok->pool_size / process_count ) {
                cok->ptr = get_connection_in_pool ( epoll_fd, cok->pool_key );

                if ( cok->ptr ) {
                    ( ( se_ptr_t * ) cok->ptr )->data = cok;
                    cok->status = 2;
                    cok->reusedtimes = 1;
                    cok->fd = ( ( se_ptr_t * ) cok->ptr )->fd;

                    lua_pushboolean ( L, 1 );

                    return 1;
                }

                /// pool full
                if ( add_waiting_get_connection ( cok ) ) {
                    cok->inuse = 1;
                    return lua_yield ( L, 0 );
                }
            }
        }

        int connect_ret = 0;
        cok->fd = tcp_connect ( host, port, cok, epoll_fd, &connect_ret );

        if ( cok->fd == -1 && cok->dns_query_fd == -1 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Init socket error!3" );
            return 2;

        } else if ( cok->fd == -2 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Init socket s_addr error!" );
            return 2;

        } else if ( cok->fd == -3 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "names lookup error!" );
            return 2;
        }

        if ( connect_ret == 0 ) {
            cok->status = 2;

            del_in_timeout_link ( cok );
            lua_pushboolean ( L, 1 );
            return 1;
            // is done

        } else if ( connect_ret == -1 && errno != EINPROGRESS ) {
            // is error
            lua_pushnil ( L );
            lua_pushstring ( L, "Init socket error!4" );
            return 2;
        }
    }
    cok->inuse = 1;
    return lua_yield ( L, 0 );
}

static int cosocket_be_write ( se_ptr_t *ptr )
{
    io_counts++;
    cosocket_t *cok = ptr->data;
    int n = 0, ret = 0;
    cok->in_read_action = 0;

    while ( ( n = send ( cok->fd, cok->send_buf + cok->send_buf_ed,
                         cok->send_buf_len - cok->send_buf_ed, MSG_DONTWAIT | MSG_NOSIGNAL ) ) > 0 ) {
        cok->send_buf_ed += n;
    }

    if ( cok->send_buf_ed == cok->send_buf_len || ( n < 0 && errno != EAGAIN
            && errno != EWOULDBLOCK ) ) {
        if ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) {
            se_delete ( cok->ptr );
            cok->ptr = NULL;
            connection_pool_counter_operate ( cok->pool_key, -1 );
            close ( cok->fd );
            cok->fd = -1;
            cok->status = 0;
            cok->send_buf_ed = 0;

        } else {
            se_be_pri ( cok->ptr, NULL );
        }

        if ( cok->send_buf_need_free ) {
            free ( cok->send_buf_need_free );
            cok->send_buf_need_free = NULL;
        }

        /*if(!del_in_timeout_link(cok)){
            printf("del error %d\n", __LINE__);
            exit(1);
        }*/
        del_in_timeout_link ( cok );

        int rc = 1;

        if ( cok->send_buf_ed >= cok->send_buf_len ) {
            lua_pushnumber ( cok->L, cok->send_buf_ed );

        } else if ( cok->fd == -1 ) {
            lua_pushnil ( cok->L );
            lua_pushstring ( cok->L, "connection closed!" );
            rc = 2;

        } else {
            lua_pushboolean ( cok->L, 0 );
        }

        cok->inuse = 0;
        lua_co_resume ( cok->L, rc );
    }

    return 0;
}

static int lua_co_send ( lua_State *L )
{
    cosocket_t *cok = NULL;
    {
        if ( lua_gettop ( L ) < 2 ) {
            return 0;
        }

        int t = lua_type ( L, 2 );

        if ( !lua_isuserdata ( L, 1 ) || ( t != LUA_TSTRING && t != LUA_TTABLE ) ) {
            lua_pushboolean ( L, 0 );
            lua_pushstring ( L, "Error params!" );
            return 2;
        }

        cok = ( cosocket_t * ) lua_touserdata ( L, 1 );

        if ( cok->status != 2 || cok->fd == -1 || !cok->ptr ) {
            lua_pushboolean ( L, 0 );
            lua_pushstring ( L, "Not connected!" );
            return 2;
        }

        if ( cok->inuse == 1 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "socket busy!" );
            return 2;
        }

        cok->L = L;
        cok->send_buf_ed = 0;

        if ( t == LUA_TTABLE ) {
            cok->send_buf_len = lua_calc_strlen_in_table ( L, 2, 2, 1 /* strict */ );

            if ( cok->send_buf_len > 0 ) {
                if ( cok->send_buf_len <= sizeof ( cok->_send_buf ) ) {
                    cok->send_buf_need_free = NULL;
                    lua_copy_str_in_table ( L, 2, cok->_send_buf );
                    cok->send_buf = cok->_send_buf;

                } else {
                    cok->send_buf_need_free = large_malloc ( cok->send_buf_len );

                    if ( !cok->send_buf_need_free ) {
                        printf ( "malloc error @%s:%d\n", __FILE__, __LINE__ );
                        exit ( 1 );
                    }

                    lua_copy_str_in_table ( L, 2, cok->send_buf_need_free );
                    cok->send_buf = cok->send_buf_need_free;
                }
            }

        } else {
            cok->send_buf = lua_tolstring ( L, 2, &cok->send_buf_len );
            cok->send_buf_need_free = NULL;
        }

        if ( cok->send_buf_len < 1 ) {
            lua_pushboolean ( L, 0 );
            lua_pushstring ( L, "content empty!" );
            return 2;
        }

        se_be_write ( cok->ptr, cosocket_be_write );

        add_to_timeout_link ( cok, cok->timeout );
    }
    cok->inuse = 1;
    return lua_yield ( L, 0 );
}

static int cosocket_be_read ( se_ptr_t *ptr )
{
    io_counts++;
    cosocket_t *cok = ptr->data;
    int n = 0, ret = 0;

init_read_buf:

    if ( !cok->read_buf
         || ( cok->last_buf->buf_len >= cok->last_buf->buf_size ) ) { /// init read buf
        cosocket_link_buf_t *nbuf = NULL;
        nbuf = malloc ( sizeof ( cosocket_link_buf_t ) );

        if ( nbuf == NULL ) {
            printf ( "malloc error @%s:%d\n", __FILE__, __LINE__ );
            exit ( 1 );
        }

        nbuf->buf = large_malloc ( 4096 );

        if ( !nbuf->buf ) {
            printf ( "malloc error @%s:%d\n", __FILE__, __LINE__ );
            exit ( 1 );
        }

        nbuf->buf_size = 4096;
        nbuf->buf_len = 0;
        nbuf->next = NULL;

        if ( cok->read_buf ) {
            cok->last_buf->next = nbuf;

        } else {
            cok->read_buf = nbuf;
        }

        cok->last_buf = nbuf;
    }

    while ( ( n = recv ( cok->fd, cok->last_buf->buf + cok->last_buf->buf_len,
                         cok->last_buf->buf_size - cok->last_buf->buf_len, 0 ) ) > 0 ) {
        cok->last_buf->buf_len += n;
        cok->total_buf_len += n;

        if ( cok->last_buf->buf_len >= cok->last_buf->buf_size ) {
            goto init_read_buf;
        }
    }

    if ( n == 0 || ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) ) {
        /// socket closed
        del_in_timeout_link ( cok );
        {
            cok->status = 0;

            se_delete ( cok->ptr );
            cok->ptr = NULL;
            connection_pool_counter_operate ( cok->pool_key, -1 );
            close ( cok->fd );
            cok->fd = -1;
            cok->status = 0;
        }

        if ( cok->in_read_action == 1 ) {
            cok->in_read_action = 0;
            int rt = lua_co_read_ ( cok );
            cok->inuse = 0;

            if ( rt > 0 ) {
                ret = lua_co_resume ( cok->L, rt );

            } else if ( n == 0 ) {
                lua_pushnil ( cok->L );
                ret = lua_co_resume ( cok->L, 1 );
            }

            if ( ret == LUA_ERRRUN ) {
                se_delete ( cok->ptr );
                cok->ptr = NULL;
                connection_pool_counter_operate ( cok->pool_key, -1 );
                close ( cok->fd );
                cok->fd = -1;
                cok->status = 0;
            }
        }

    } else {
        if ( cok->in_read_action == 1 ) {
            int rt = lua_co_read_ ( cok );

            if ( rt > 0 ) {
                cok->in_read_action = 0;
                /*if(!del_in_timeout_link(cok)){
                    printf("del error %d\n", __LINE__);
                    exit(1);
                }*/
                del_in_timeout_link ( cok );
                cok->inuse = 0;
                ret = lua_co_resume ( cok->L, rt );

                if ( ret == LUA_ERRRUN ) {
                    se_delete ( cok->ptr );
                    cok->ptr = NULL;
                    connection_pool_counter_operate ( cok->pool_key, -1 );
                    close ( cok->fd );
                    cok->fd = -1;
                    cok->status = 0;
                }
            }
        }
    }

    return 0;
}

int lua_co_read_ ( cosocket_t *cok )
{
    if ( cok->total_buf_len < 1 ) {
        if ( cok->status == 0 ) {
            lua_pushnil ( cok->L );
            lua_pushstring ( cok->L, "Not connected!" );
            return 2;
        }

        return 0;
    }

    size_t be_copy = cok->buf_read_len;

    if ( cok->buf_read_len == -1 ) { // read line
        int i = 0;
        int oi = 0;
        int has = 0;
        cosocket_link_buf_t *nbuf = cok->read_buf;

        while ( nbuf ) {
            for ( i = 0; i < nbuf->buf_len; i++ ) {
                if ( nbuf->buf[i] == '\n' ) {
                    has = 1;
                    break;
                }
            }

            if ( has == 1 ) {
                break;
            }

            oi += i;
            nbuf = nbuf->next;
        }

        i += oi;

        if ( has == 1 ) {
            i += 1;
            be_copy = i;

        } else {
            return 0;
        }

    } else if ( cok->buf_read_len == -2 ) {
        be_copy = cok->total_buf_len;
    }

    if ( cok->status == 0 ) {
        if ( be_copy > cok->total_buf_len ) {
            be_copy = cok->total_buf_len;
        }
    }

    int kk = 0;

    if ( be_copy > 0 && cok->total_buf_len >= be_copy ) {
        char *buf2lua = large_malloc ( be_copy );

        if ( !buf2lua ) {
            printf ( "malloc error @%s:%d\n", __FILE__, __LINE__ );
            exit ( 1 );
        }

        size_t copy_len = be_copy;
        size_t copy_ed = 0;
        int this_copy_len = 0;
        cosocket_link_buf_t *bf = NULL;

        while ( cok->read_buf ) {
            this_copy_len = ( cok->read_buf->buf_len + copy_ed > copy_len ? copy_len - copy_ed :
                              cok->read_buf->buf_len );

            if ( this_copy_len > 0 ) {
                memcpy ( buf2lua + copy_ed, cok->read_buf->buf, this_copy_len );
                copy_ed += this_copy_len;
                memmove ( cok->read_buf->buf, cok->read_buf->buf + this_copy_len,
                          cok->read_buf->buf_len - this_copy_len );
                cok->read_buf->buf_len -= this_copy_len;
            }

            if ( copy_ed >= be_copy ) { /// not empty
                cok->total_buf_len -= copy_ed;

                if ( cok->buf_read_len == -1 ) { /// read line , cut the \r \n
                    if ( buf2lua[be_copy - 1] == '\n' ) {
                        be_copy -= 1;
                    }

                    if ( buf2lua[be_copy - 1] == '\r' ) {
                        be_copy -= 1;
                    }
                }

                lua_pushlstring ( cok->L, buf2lua, be_copy );
                free ( buf2lua );
                return 1;

            } else {
                bf = cok->read_buf;
                cok->read_buf = cok->read_buf->next;

                if ( cok->last_buf == bf ) {
                    cok->last_buf = NULL;
                }

                free ( bf->buf );
                free ( bf );
            }
        }

        free ( buf2lua );
    }

    return 0;
}

static int lua_co_read ( lua_State *L )
{
    cosocket_t *cok = NULL;
    {
        if ( !lua_isuserdata ( L, 1 ) ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Error params!" );
            return 2;
        }

        cok = ( cosocket_t * ) lua_touserdata ( L, 1 );

        if ( ( cok->status != 2 || cok->fd == -1 || !cok->ptr ) && cok->total_buf_len < 1 ) {
            lua_pushnil ( L );
            lua_pushfstring ( L, "Not connected! %d %d", cok->status, cok->fd );
            return 2;
        }

        if ( cok->inuse == 1 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "socket busy!" );
            return 2;
        }

        cok->L = L;
        cok->buf_read_len = -2; /// read line

        if ( lua_isnumber ( L, 2 ) ) {
            cok->buf_read_len = lua_tonumber ( L, 2 );

            if ( cok->buf_read_len < 0 ) {
                cok->buf_read_len = 0;
                lua_pushnil ( L );
                lua_pushstring ( L, "Error params!" );
                return 2;
            }

        } else {
            if ( lua_isstring ( L, 2 ) ) {
                if ( strcmp ( "*l", lua_tostring ( L, 2 ) ) == 0 ) {
                    cok->buf_read_len = -1;    /// read all
                }
            }
        }

        int rt = lua_co_read_ ( cok );

        if ( rt > 0 ) {
            return rt; // has buf
        }

        if ( cok->fd == -1 ) {
            lua_pushnil ( L );
            lua_pushstring ( L, "Not connected!" );
            return 2;
        }

        if ( cok->in_read_action != 1 ) {
            cok->in_read_action = 1;
            se_be_read ( cok->ptr, cosocket_be_read );
        }

        add_to_timeout_link ( cok, cok->timeout );
    }
    cok->inuse = 1;
    return lua_yield ( L, 0 );
}

static int _lua_co_close ( lua_State *L, cosocket_t *cok )
{
    if ( cok->read_buf ) {
        cosocket_link_buf_t *fr = cok->read_buf;
        cosocket_link_buf_t *nb = NULL;

        while ( fr ) {
            nb = fr->next;
            free ( fr->buf );
            free ( fr );
            fr = nb;
        }

        cok->read_buf = NULL;
    }

    if ( cok->send_buf_need_free ) {
        free ( cok->send_buf_need_free );
        cok->send_buf_need_free = NULL;
    }

    del_in_timeout_link ( cok );
    cok->status = 0;

    if ( cok->dns_query_fd > -1 ) {
        se_delete ( cok->ptr );
        close ( cok->dns_query_fd );
    }

    if ( cok->fd > -1 ) {
        if ( cok->pool_size < 1
             || add_connection_to_pool ( epoll_fd, cok->pool_key, cok->pool_size, cok->ptr ) == 0 ) {
            se_delete ( cok->ptr );
            connection_pool_counter_operate ( cok->pool_key, -1 );
            close ( cok->fd );
        }

        cok->ptr = NULL;
        cok->fd = -1;
    }
}

static int lua_co_close ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Error params!" );
        return 2;
    }

    cosocket_t *cok = ( cosocket_t * ) lua_touserdata ( L, 1 );

    if ( cok->status != 2 ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Not connected!" );
        return 2;
    }

    if ( cok->inuse == 1 ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "socket busy!" );
        return 2;
    }

    _lua_co_close ( L, cok );
    return 0;
}

static int lua_co_gc ( lua_State *L )
{
    cosocket_t *cok = ( cosocket_t * ) lua_touserdata ( L, 1 );
    _lua_co_close ( L, cok );
    return 0;
}

int lua_co_getreusedtimes ( lua_State *L )
{
    cosocket_t *cok = ( cosocket_t * ) lua_touserdata ( L, 1 );
    lua_pushnumber ( L, cok->reusedtimes );
    return 1;
}

int lua_co_settimeout ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) || !lua_isnumber ( L, 2 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Error params!" );
        return 2;
    }

    cosocket_t *cok = ( cosocket_t * ) lua_touserdata ( L, 1 );
    cok->timeout = lua_tonumber ( L, 2 );
    return 0;
}

int lua_co_setkeepalive ( lua_State *L )
{
    if ( !lua_isuserdata ( L, 1 ) || !lua_isnumber ( L, 2 ) ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "Error params!" );
        return 2;
    }

    cosocket_t *cok = ( cosocket_t * ) lua_touserdata ( L, 1 );
    cok->pool_size = lua_tonumber ( L, 2 );

    if ( cok->pool_size < 0 || cok->pool_size > 1000 ) {
        cok->pool_size = 0;
    }

    if ( lua_gettop ( L ) == 3 && lua_isstring ( L, 3 ) ) {
        size_t len = 0;
        const char *key = lua_tolstring ( L, 3, &len );
        cok->pool_key = fnv1a_32 ( key, len );
    }

    lua_pushboolean ( L, 1 );
    return 1;
}


/* This is luaL_setfuncs() from Lua 5.2 alpha */
static void setfuncs ( lua_State *L, const luaL_Reg *l, int nup )
{
    luaL_checkstack ( L, nup, "too many upvalues" );

    for ( ; l && l->name; l++ ) { /* fill the table with given functions */
        int i = 0;

        for ( i = 0; i < nup; i++ ) { /* copy upvalues to the top */
            lua_pushvalue ( L, -nup );
        }

        lua_pushcclosure ( L, l->func, nup ); /* closure with those upvalues */
        lua_setfield ( L, - ( nup + 2 ), l->name );
    }

    lua_pop ( L, nup ); /* remove upvalues */
}
/* End of luaL_setfuncs() from Lua 5.2 alpha */

static const luaL_reg M[] = {
    {"connect", lua_co_connect},
    {"send", lua_co_send},
    {"read", lua_co_read},
    {"receive", lua_co_read},
    {"settimeout", lua_co_settimeout},
    {"setkeepalive", lua_co_setkeepalive},
    {"getreusedtimes", lua_co_getreusedtimes},

    {"close", lua_co_close},
    {"__gc", lua_co_gc},

    {NULL, NULL}
};

static int lua_co_tcp ( lua_State *L )
{
    cosocket_t *cok = NULL;
    cok = ( cosocket_t * ) lua_newuserdata ( L, sizeof ( cosocket_t ) );
    cok->type = 2;
    cok->L = L;
    cok->inuse = 0;
    cok->ptr = NULL;
    cok->fd = -1;
    cok->status = 0;
    cok->read_buf = NULL;
    cok->send_buf_need_free = NULL;
    cok->total_buf_len = 0;
    cok->buf_read_len = 0;
    cok->timeout = 30000;
    cok->dns_query_fd = -1;
    cok->in_read_action = 0;
    cok->pool_size = 0;
    cok->pool_key = 0;

    if ( luaL_newmetatable ( L, "cosocket" ) ) {
        /* Module table to be set as upvalue */
        //luaL_checkstack(L, 1, "not enough stack to register connection MT");
        lua_pushvalue ( L, lua_upvalueindex ( 1 ) );
        setfuncs ( L, M, 1 );
        lua_pushvalue ( L, -1 );
        lua_setfield ( L, -2, "__index" );
    }

    lua_setmetatable ( L, -2 );
}

int swop_counter = 0;
cosocket_swop_t *swop_top = NULL;
cosocket_swop_t *swop_lat = NULL;

int lua_f_coroutine_wait ( lua_State *L )
{
    {
        if ( !lua_isthread ( L, 1 ) ) {
            return 0;
        }

        lua_State *JL = lua_tothread ( L, 1 );
        int st = lua_status ( JL );

        if ( st != LUA_YIELD ) {
            if ( st == 0 ) {
                lua_pushstring ( L, "allthreads" );
                lua_gettable ( L, LUA_GLOBALSINDEX );
                lua_pushvalue ( L, -2 );
                lua_gettable ( L, -2 );

                if ( lua_istable ( L, -1 ) ) {
                    size_t l = lua_objlen ( L, -1 );
                    int i = 0;

                    for ( i = 0; i < l; i++ ) {
                        lua_rawgeti ( L, 0 - ( i + 1 ), i + 1 );
                    }

                    return l;
                }

                return 0;
            }

            /// get returns to tmp table (may have)
            int rts = lua_gettop ( JL );
            lua_xmove ( JL, L, rts );
            int ret = lua_resume ( JL, 0 );

            if ( ret == LUA_ERRRUN ) {
                lua_pop ( JL, -1 );
            }

            return rts;
        }

        char key[32];
        sprintf ( key, "%x__be_resume", JL );
        lua_pushlightuserdata ( JL, L );
        lua_setglobal ( JL, key );
        lua_pop ( L, 1 );
    }
    return lua_yield ( L, 0 );
}

int lua_f_coroutine_resume_waiting ( lua_State *L )
{
    char key[32];
    sprintf ( key, "%x__be_resume", L );
    lua_getglobal ( L, key );

    if ( LUA_TLIGHTUSERDATA == lua_type ( L, -1 ) ) {
        cosocket_swop_t *swop = malloc ( sizeof ( cosocket_swop_t ) );

        if ( swop == NULL ) {
            printf ( "malloc error @%s:%d\n", __FILE__, __LINE__ );
            exit ( 1 );
        }

        swop->L = L;
        swop->next = NULL;

        if ( swop_lat != NULL ) {
            swop_lat->next = swop;

        } else {
            swop_top->next = swop;
        }

        swop_lat = swop;
        lua_State *_L = lua_touserdata ( L, -1 );
        lua_pushnil ( L );
        lua_setglobal ( L, key );
        lua_pop ( L, 1 );

        if ( lua_status ( _L ) == LUA_YIELD ) {
            int rts = lua_gettop ( L );
            lua_xmove ( L, _L, rts );
            int ret = lua_resume ( _L, rts );

            if ( ret == LUA_ERRRUN && lua_isstring ( _L, -1 ) ) {
                printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( _L, -1 ) );
                lua_pop ( _L, -1 );
            }
        }

    } else {
        if ( lua_type ( L, -2 ) == LUA_TNIL ) { /// is error back
            lua_pop ( L, 1 );
            return 2;

        } else {
            lua_pop ( L, -1 );
            lua_pushthread ( L );
            return 1;
        }
    }

    return 0;
}

int lua_f_coroutine_swop ( lua_State *L )
{
    if ( swop_counter++ < 800 ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    cosocket_swop_t *swop = malloc ( sizeof ( cosocket_swop_t ) );

    if ( swop == NULL ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    swop_counter = 0;
    swop->L = L;
    swop->next = NULL;

    if ( swop_lat != NULL ) {
        swop_lat->next = swop;

    } else {
        swop_top->next = swop;
    }

    swop_lat = swop;
    return lua_yield ( L, 0 );
}

static lua_State *job_L = NULL;

void set_epoll_fd ( int fd, int _process_count ) /// for alilua-serv
{
    epoll_fd = fd;

    if ( _process_count > 1 ) {
        process_count = _process_count;
    }

    lua_getglobal ( LM, "clearthreads" );
    clearthreads_handler = luaL_ref ( LM, LUA_REGISTRYINDEX );
}

void add_io_counts()  /// for alilua-serv
{
    io_counts += 2;
}

time_t chk_time = 0;
int do_other_jobs()
{
    io_counts ++;
    swop_counter = swop_counter / 2;

    if ( io_counts >= 10000 ) {
        io_counts = 0;

        if ( clearthreads_handler != 0 ) {
            lua_rawgeti ( LM, LUA_REGISTRYINDEX, clearthreads_handler );

            if ( lua_pcall ( LM, 0, 0, 0 ) ) {
                printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( LM, -1 ) );
                lua_pop ( LM, -1 );
                exit ( 0 );
            }
        }
    }

    time ( &timer );

    if ( timer - chk_time > 0 ) {
        chk_time = timer;
        chk_do_timeout_link ( epoll_fd );
        get_connection_in_pool ( epoll_fd, 0 );
    }

    /// resume swops
    {
        cosocket_swop_t *swop = NULL;

        if ( swop_top->next != NULL ) {
            swop = swop_top->next;
            swop_top->next = swop->next;

            if ( swop_top->next == NULL ) {
                swop_lat = NULL;
            }

            lua_State *L = swop->L;
            free ( swop );
            swop = NULL;

            if ( lua_status ( L ) != 0 ) {
                lua_pushboolean ( L, 1 );
                int ret = lua_resume ( L, 1 );

                if ( ret == LUA_ERRRUN && lua_isstring ( L, -1 ) ) {
                    printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( L, -1 ) );
                    lua_pop ( L, -1 );
                    lua_f_coroutine_resume_waiting ( L );
                }
            }
        }
    }
}

int _do_other_jobs()
{
    do_other_jobs();

    if ( lua_status ( job_L ) != LUA_YIELD ) {
        return 0;
    }

    return 1;
}

static const struct luaL_reg cosocket_methods[] = {
    { "tcp", lua_co_tcp },
    { NULL, NULL }
};

int lua_f_startloop ( lua_State *L )
{
    if ( epoll_fd == -1 ) {
        epoll_fd = se_create ( 4096 );
    }

    luaL_argcheck ( L, lua_isfunction ( L, 1 )
                    && !lua_iscfunction ( L, 1 ), 1, "Lua function expected" );
    job_L = lua_newthread ( L );
    lua_pushvalue ( L, 1 ); /* move function to top */
    lua_xmove ( L, job_L, 1 ); /* move function from L to job_L */

    if ( lua_resume ( job_L, 0 ) != LUA_YIELD && lua_isstring ( job_L, -1 ) ) {
        luaL_error ( L, lua_tostring ( job_L, -1 ) );
        lua_pop ( job_L, -1 );
    }

    lua_getglobal ( job_L, "clearthreads" );
    clearthreads_handler = luaL_ref ( job_L, LUA_REGISTRYINDEX );
    LM = job_L;

    se_loop ( epoll_fd, 10, _do_other_jobs );

    return 0;
}

int luaopen_coevent ( lua_State *L )
{
    LM = L;
    epoll_fd = -1;

    swop_top = malloc ( sizeof ( cosocket_swop_t ) );
    swop_top->next = NULL;

    lua_pushlightuserdata ( L, NULL );
    lua_setglobal ( L, "null" );

    lua_register ( L, "startloop", lua_f_startloop );
    lua_register ( L, "coroutine_wait", lua_f_coroutine_wait );
    lua_register ( L, "coroutine_resume_waiting", lua_f_coroutine_resume_waiting );
    lua_register ( L, "swop", lua_f_coroutine_swop );
    lua_register ( L, "sha1bin", lua_f_sha1bin );
    lua_register ( L, "base64_encode", lua_f_base64_encode );
    lua_register ( L, "base64_decode", lua_f_base64_decode );
    lua_register ( L, "escape", cosocket_lua_f_escape );
    lua_register ( L, "escape_uri", lua_f_escape_uri );
    lua_register ( L, "unescape_uri", lua_f_unescape_uri );
    lua_register ( L, "time", lua_f_time );
    lua_register ( L, "longtime", lua_f_longtime );

    luaL_loadstring ( L, "_coresume = coroutine.resume \
_cocreate = coroutine.create \
allthreads = {} \
function clearthreads() \
	local v \
	local c = 0 \
	for v in pairs(allthreads) do \
		if coroutine.status(v) ~= 'suspended' then \
			allthreads[v] = nil \
		else c=c+1 \
		end \
	end \
	collectgarbage() \
end \
function wait(...) \
	local arg = {...} \
	local k,v = #arg \
	if k == 1 then \
		if type(arg[1]) == 'table' then \
			local rts = {} \
			for k,v in ipairs(arg[1]) do \
				rts[k] = coroutine_wait(v) \
			end \
			return rts \
		else \
			return coroutine_wait(arg[1]) \
		end \
	elseif k > 0 then \
		local rts = {} \
		for k,v in ipairs(arg) do \
			rts[k] = coroutine_wait(v) \
		end \
		return rts \
	end \
end \
function newthread(f,n1,n2,n3,n4,n5) local F = _cocreate(function(n1,n2,n3,n4,n5) local R = {f(n1,n2,n3,n4,n5)} local t = coroutine_resume_waiting(unpack(R)) if t then allthreads[t] = R end return unpack(R) end) local r,e = _coresume(F,n1,n2,n3,n4,n5) if e then return nil,e end allthreads[F]=1 return F end" );

    lua_pcall ( L, 0, 0, 0 );

    static const struct luaL_reg _MT[] = {{NULL, NULL}};
    luaL_openlib ( L, "cosocket", _MT, 0 );

    if ( luaL_newmetatable ( L, "cosocket*" ) ) {
        luaL_register ( L, NULL, _MT );
        lua_pushliteral ( L, "cosocket*" );
        lua_setfield ( L, -2, "__metatable" );
    }

    lua_setmetatable ( L, -2 );
    lua_pushvalue ( L, -1 );
    setfuncs ( L, cosocket_methods, 1 );
    lua_pushcfunction ( L, lua_f_startloop );
    return 1;
}
