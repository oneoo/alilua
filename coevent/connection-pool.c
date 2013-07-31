#include "coevent.h"
#include "connection-pool.h"

void *cosocket_connection_pool_counters[64] = {NULL32 NULL32};
void *connect_pool_p[2][64] = {{NULL32 NULL32}, {NULL32 NULL32}};
int connect_pool_ttl = 30; /// cache times

static int cosocket_be_close ( se_ptr_t *ptr )
{
    //printf("cosocket_be_close %d\n", ptr->fd);
    int fd = ptr->fd;

    cosocket_connection_pool_t *n = ptr->data;
    int k = n->pool_key % 64;

    if ( n == connect_pool_p[0][k] ) {
        connect_pool_p[0][k] = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = NULL;
        }

    } else if ( n == connect_pool_p[1][k] ) {
        connect_pool_p[1][k] = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = NULL;
        }

    } else {
        ( ( cosocket_connection_pool_t * ) n->uper )->next = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = n->uper;
        }
    }

    se_delete ( n->ptr );
    n->ptr = NULL;

    if ( n->ssl ) {
        SSL_CTX_free ( n->ctx );
        n->ctx = NULL;
        SSL_free ( n->ssl );
        n->ssl = NULL;
    }

    connection_pool_counter_operate ( n->pool_key, -1 );

    free ( n );

    close ( fd );
}

cosocket_connection_pool_counter_t *get_connection_pool_counter (
    unsigned long pool_key )
{
    int k = pool_key % 64;
    cosocket_connection_pool_counter_t *n = cosocket_connection_pool_counters[k];
    cosocket_connection_pool_counter_t *u = NULL;

    while ( n && n->pool_key != pool_key ) {
        u = n;
        n = n->next;
    }

    if ( !n ) {
        n = malloc ( sizeof ( cosocket_connection_pool_counter_t ) );
        n->pool_key = pool_key;
        n->count = 0;
        n->next = NULL;
        n->uper = NULL;

        if ( cosocket_connection_pool_counters[k] == NULL ) { // at top
            cosocket_connection_pool_counters[k] = n;

        } else if ( u ) {
            n->uper = u;
            u->next = n;
        }
    }

    return n;
}

void connection_pool_counter_operate ( unsigned long pool_key, int a )
{
    /// add: connection_pool_counter_operate(key, 1);
    /// remove: connection_pool_counter_operate(key, -1);
    if ( pool_key < 1 ) {
        return;
    }

    cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter (
                pool_key );
    pool_counter->count += a;
}

void *waiting_get_connections[64] = {NULL32 NULL32};
void *waiting_get_connections_end[64] = {NULL32 NULL32};

int add_waiting_get_connection ( cosocket_t *cok )
{
    if ( cok->pool_key < 1 ) {
        return 0;
    }

    int k = cok->pool_key % 64;
    cosocket_waiting_get_connection_t *n = malloc ( sizeof (
            cosocket_waiting_get_connection_t ) );

    if ( !n ) {
        return 0;
    }

    n->cok = cok;
    n->next = NULL;
    n->uper = NULL;

    if ( waiting_get_connections[k] == NULL ) {
        waiting_get_connections[k] = n;
        waiting_get_connections_end[k] = n;
        return 1;

    } else {
        ( ( cosocket_waiting_get_connection_t * ) waiting_get_connections_end[k] )->next = n;
        n->uper = waiting_get_connections_end[k];
        waiting_get_connections_end[k] = n;
    }
}

se_ptr_t *get_connection_in_pool ( int epoll_fd, unsigned long pool_key,
                                   cosocket_t *cok )
{
    int k = pool_key % 64;
    int p = ( timer / connect_pool_ttl ) % 2;
    cosocket_connection_pool_t  *n = NULL,
                                 *m = NULL,
                                  *nn = NULL;
    se_ptr_t *ptr = NULL;

    /// clear old caches
    int q = ( p + 1 ) % 2;
    int i = 0;

    for ( i = 0; i < 64; i++ ) {
        n = connect_pool_p[q][i];

        while ( n ) {
            m = n;
            n = n->next;

            ptr = m->ptr;

            if ( ptr->z == 0 ) { /// recached
                ptr->z = 1;
                nn = connect_pool_p[p][m->pool_key % 64];

                if ( nn == NULL ) {
                    connect_pool_p[p][m->pool_key % 64] = m;
                    m->next = NULL;
                    m->uper = NULL;

                } else {
                    m->uper = NULL;
                    m->next = nn;
                    nn->uper = m;
                    connect_pool_p[p][m->pool_key % 64] = m;
                }

            } else {
                int fd = ptr->fd;
                se_delete ( ptr );
                close ( fd );
                connection_pool_counter_operate ( m->pool_key, -1 );
                free ( m );
            }
        }

        connect_pool_p[q][i] = NULL;
    }

    /// end
    if ( pool_key == 0 ) {
        return NULL; /// only do clear job
    }

regetfd:
    n = connect_pool_p[p][k];
    int ii = 0;

    while ( n != NULL ) {
        if ( n->pool_key == pool_key ) {
            break;
        }

        n = ( cosocket_connection_pool_t * ) n->next;
    }

    if ( n ) {
        if ( n == connect_pool_p[p][k] ) { /// at top
            m = n->next;

            if ( m ) {
                m->uper = NULL;
                connect_pool_p[p][k] = m;

            } else {
                connect_pool_p[p][k] = NULL;
            }

        } else {
            ( ( cosocket_connection_pool_t * ) n->uper )->next = n->next;

            if ( n->next ) {
                ( ( cosocket_connection_pool_t * ) n->next )->uper = n->uper;
            }
        }

        ptr = n->ptr;

        if ( cok ) {
            cok->ssl = n->ssl;
            cok->ctx = n->ctx;
        }

        free ( n );
        //printf ( "get fd in pool%d %d key:%d\n", p, ptr->fd, k );
        return ptr;
    }

    if ( p != q ) {
        p = q;
        goto regetfd;
    }

    return NULL;
}

int add_connection_to_pool ( int epoll_fd, unsigned long pool_key, int pool_size,
                             se_ptr_t *ptr, void *ssl, void *ctx )
{
    if ( pool_key < 0 || !ptr || ptr->fd < 0 ) {
        return 0;
    }

    int k = pool_key % 64;
    int i = 0;
    /// check waiting list
    {
        cosocket_waiting_get_connection_t *n = waiting_get_connections[k];

        if ( n != NULL ) {
            while ( n && ( ( cosocket_t * ) n->cok )->pool_key != pool_key ) {
                n = n->next;
            }

            if ( n ) {
                if ( n->uper ) {
                    ( ( cosocket_waiting_get_connection_t * ) n->uper )->next = n->next;

                    if ( n->next ) {
                        ( ( cosocket_waiting_get_connection_t * ) n->next )->uper = n->uper;
                    }

                } else {
                    waiting_get_connections[k] = n->next;

                    if ( n->next ) {
                        ( ( cosocket_waiting_get_connection_t * ) n->next )->uper = NULL;
                    }
                }

                cosocket_t *_cok = n->cok;

                if ( n == waiting_get_connections_end[k] ) {
                    waiting_get_connections_end[k] = n->uper;
                }

                free ( n );

                _cok->ssl = ssl;
                _cok->ctx = ctx;
                _cok->ptr = ptr;
                ptr->data = _cok;
                _cok->fd = ptr->fd;
                _cok->status = 2;
                _cok->reusedtimes = 1;
                _cok->inuse = 0;

                se_be_pri ( ptr, NULL );

                lua_pushboolean ( _cok->L, 1 );

                if ( lua_resume ( _cok->L, 1 ) == LUA_ERRRUN ) {
                    if ( lua_isstring ( _cok->L, -1 ) ) {
                        printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( _cok->L, -1 ) );
                        lua_pop ( _cok->L, -1 );
                    }
                }

                return 1;
            }
        }
    }
    /// end

    time ( &timer );
    int p = ( timer / connect_pool_ttl ) % 2;

    cosocket_connection_pool_t  *n = NULL,
                                 *m = NULL;
    n = connect_pool_p[p][k];

    if ( n == NULL ) {
        m = malloc ( sizeof ( cosocket_connection_pool_t ) );

        if ( m == NULL ) {
            return 0;
        }

        ptr->z = 0; // recached
        m->ptr = ptr;
        m->pool_key = pool_key;
        m->ssl = ssl;
        m->ctx = ctx;
        m->next = NULL;
        m->uper = NULL;

        connect_pool_p[p][k] = m;

        ptr->data = m;
        n = connect_pool_p[p][k];

        se_be_read ( ptr, cosocket_be_close );

        return 1;

    } else {
        int in_pool = 0;

        while ( n != NULL ) {
            if ( n->pool_key == pool_key ) {
                if ( in_pool++ >= pool_size ) { /// pool full
                    return 0;
                }
            }

            if ( n->next == NULL ) { /// last
                m = malloc ( sizeof ( cosocket_connection_pool_t ) );

                if ( m == NULL ) {
                    return 0;
                }

                //m->type = EPOLL_PTR_TYPE_COSOCKET_WAIT;
                ptr->z = 0; // recached
                m->ptr = ptr;
                m->pool_key = pool_key;
                m->ssl = ssl;
                m->ctx = ctx;
                m->next = NULL;
                m->uper = n;
                //m->fd = fd;
                n->next = m;

                ptr->data = m;
                se_be_read ( ptr, cosocket_be_close );
                return 1;
            }

            n = ( cosocket_connection_pool_t * ) n->next;
        }
    }

    return 0;
}
