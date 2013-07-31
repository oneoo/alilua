#include "coevent.h"
#include "connection-pool.h"

long longtime()
{
    struct timeb t;
    ftime ( &t );
    return 1000 * t.time + t.millitm;
}

int coevent_setblocking ( int fd, int blocking )
{
    int flags = fcntl ( fd, F_GETFL, 0 );

    if ( flags == -1 ) {
        return 0;
    }

    if ( blocking ) {
        flags &= ~O_NONBLOCK;

    } else {
        flags |= O_NONBLOCK;
    }

    return fcntl ( fd, F_SETFL, flags ) != -1;
}

static struct hostent *localhost_ent = NULL;

int tcp_connect ( const char *host, int port, cosocket_t *cok, int epoll_fd, int *ret )
{
    int sockfd = -1;
    bzero ( &cok->addr, sizeof ( struct sockaddr_in ) );

    if ( port > 0 ) {
        cok->addr.sin_family = AF_INET;
        cok->addr.sin_port = htons ( port );
        cok->addr.sin_addr.s_addr = inet_addr ( host ); //按IP初始化

        if ( cok->addr.sin_addr.s_addr == INADDR_NONE ) { //如果输入的是域名
            int is_localhost = ( strcmp ( host, "localhost" ) == 0 );
            struct hostent *phost = localhost_ent;
            int in_cache = 0;

            if ( !is_localhost || localhost_ent == NULL ) {
                if ( is_localhost ) {
                    phost = ( struct hostent * ) gethostbyname ( host );

                } else {
                    if ( get_dns_cache ( host, &cok->addr.sin_addr ) ) {
                        in_cache = 1;

                    } else {
                        cok->fd = -1;

                        if ( !do_dns_query ( epoll_fd, cok, host ) ) {
                            return -3;
                        }

                        add_to_timeout_link ( cok, cok->timeout / 2 );
                        *ret = EINPROGRESS;
                        return sockfd;
                    }
                }

                if ( is_localhost ) {
                    if ( localhost_ent == NULL ) {
                        localhost_ent = malloc ( sizeof ( struct hostent ) );
                    }

                    memcpy ( localhost_ent, phost, sizeof ( struct hostent ) );
                }
            }

            if ( in_cache == 0 ) {
                if ( phost == NULL ) {
                    close ( sockfd );
                    return -2;
                }

                cok->addr.sin_addr.s_addr = ( ( struct in_addr * ) phost->h_addr )->s_addr;
            }
        }
    }

    if ( cok->pool_size > 0 ) {
        cok->ptr = get_connection_in_pool ( epoll_fd, cok->pool_key, cok );
    }

    if ( !cok->ptr ) {
        if ( ( sockfd = socket ( port > 0 ? AF_INET : AF_UNIX, SOCK_STREAM, 0 ) ) < 0 ) {
            return -1;
        }

        if ( !coevent_setblocking ( sockfd , 0 ) ) {
            close ( sockfd );
            return -1;
        }

        cok->fd = sockfd;
        connection_pool_counter_operate ( cok->pool_key, 1 );
        cok->reusedtimes = 0;

    } else {
        cok->fd = ( ( se_ptr_t * ) cok->ptr )->fd;
        ( ( se_ptr_t * ) cok->ptr )->data = cok;
        cok->reusedtimes = 1;
        *ret = 0;
        return cok->fd;
    }

    cok->ptr = se_add ( epoll_fd, sockfd, cok );
    se_be_write ( cok->ptr, cosocket_be_connected );

    add_to_timeout_link ( cok, cok->timeout / 2 );

    if ( port > 0 ) {
        *ret = connect ( sockfd, ( struct sockaddr * ) &cok->addr,
                         sizeof ( struct sockaddr_in ) );

    } else { /// connect to unix domain socket
        struct sockaddr_un un;

        memset ( &un, 0, sizeof ( struct sockaddr_un ) );
        strcpy ( un.sun_path, host );
        un.sun_family = AF_UNIX;
        int length = offsetof ( struct sockaddr_un, sun_path ) + strlen ( un.sun_path );

        *ret = connect ( sockfd, ( struct sockaddr * ) &un, length );
    }

    return sockfd;
}

static void *timeout_links[64] = {NULL32 NULL32};

int add_to_timeout_link ( cosocket_t *cok, int timeout )
{
    int p = ( ( long ) cok ) % 64;
    timeout_link_t  *_tl = NULL,
                     *_tll = NULL,
                      *_ntl = NULL;
    int add = 0;

    if ( timeout < 10 ) {
        timeout = 1000;
    }

    if ( timeout_links[p] == NULL ) {
        _ntl = malloc ( sizeof ( timeout_link_t ) );

        if ( _ntl == NULL ) {
            return 0;
        }

        _ntl->cok = cok;
        _ntl->uper = NULL;
        _ntl->next = NULL;
        _ntl->timeout = longtime() + timeout;
        timeout_links[p] = _ntl;
        return 1;

    } else {
        add = 1;
        _tl = timeout_links[p];

        while ( _tl ) {
            _tll = _tl; /// get last item

            if ( _tl->cok == cok ) {
                add = 0;
                break;
            }

            _tl = _tl->next;
        }

        if ( _tll != NULL ) {
            _ntl = malloc ( sizeof ( timeout_link_t ) );

            if ( _ntl == NULL ) {
                return 0;
            }

            _ntl->cok = cok;
            _ntl->uper = _tll;
            _ntl->next = NULL;
            _ntl->timeout = longtime() + timeout;
            _tll->next = _ntl;
            return 1;
        }
    }

    return 0;
}

int del_in_timeout_link ( cosocket_t *cok )
{
    int p = ( ( long ) cok ) % 64;
    timeout_link_t  *_tl = NULL,
                     *_utl = NULL,
                      *_ntl = NULL;

    if ( timeout_links[p] == NULL ) {
        return 0;

    } else {
        _tl = timeout_links[p];

        while ( _tl ) {
            if ( _tl->cok == cok ) {
                _utl = _tl->uper;
                _ntl = _tl->next;

                if ( _utl == NULL ) {
                    timeout_links[p] = _ntl;

                    if ( _ntl != NULL ) {
                        _ntl->uper = NULL;
                    }

                } else {
                    _utl->next = _tl->next;

                    if ( _ntl != NULL ) {
                        _ntl->uper = _utl;
                    }
                }

                free ( _tl );
                return 1;
            }

            _tl = _tl->next;
        }
    }

    return 0;
}

int chk_do_timeout_link ( int epoll_fd )
{
    long nt = longtime();
    timeout_link_t  *_tl = NULL,
                     *_ttl = NULL,
                      *_utl = NULL,
                       *_ntl = NULL;
    struct epoll_event ev;
    int i = 0;

    for ( i = 0; i < 64; i++ ) {
        if ( timeout_links[i] == NULL ) {
            continue;
        }

        _tl = timeout_links[i];

        while ( _tl ) {
            _ttl = _tl->next;

            if ( nt >= _tl->timeout ) {
                _utl = _tl->uper;
                _ntl = _tl->next;

                if ( _utl == NULL ) {
                    timeout_links[i] = _ntl;

                    if ( _ntl != NULL ) {
                        _ntl->uper = NULL;
                    }

                } else {
                    _utl->next = _tl->next;

                    if ( _ntl != NULL ) {
                        _ntl->uper = _utl;
                    }
                }

                cosocket_t *cok = _tl->cok;
                free ( _tl );

                //printf("fd timeout %d %d %ld\n", cok->fd,cok->dns_query_fd, _tl->timeout);

                if ( cok->dns_query_fd > -1 ) {
                    se_delete ( cok->ptr );
                    cok->ptr = NULL;
                    close ( cok->dns_query_fd );
                    cok->dns_query_fd = -1;

                } else {
                    se_delete ( cok->ptr );
                    cok->ptr = NULL;
                    close ( cok->fd );
                    connection_pool_counter_operate ( cok->pool_key, -1 );
                    cok->fd = -1;
                    cok->status = 0;
                }

                if ( cok->ssl ) {
                    SSL_shutdown ( cok->ssl );
                    SSL_CTX_free ( cok->ctx );
                    cok->ctx = NULL;
                    SSL_free ( cok->ssl );
                    cok->ssl = NULL;
                }

                lua_pushnil ( cok->L );
                lua_pushstring ( cok->L, "timeout!" );
                cok->inuse = 0;

                lua_co_resume ( cok->L, 2 );
            }

            _tl = _ttl;
        }
    }
}
