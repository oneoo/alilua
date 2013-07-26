#include "coevent.h"
#include "connection-pool.h"

#define NTOHS(p) (((p)[0] << 8) | (p)[1])

void *dns_cache[3][64] = {{NULL32 NULL32}, {NULL32 NULL32}, {NULL32 NULL32}};
int dns_cache_ttl = 180;

int get_dns_cache ( const char *name, struct in_addr *addr )
{
    int p = ( timer / dns_cache_ttl ) % 3;
    dns_cache_item_t *n = NULL,
                      *m = NULL;
    /// clear old caches
    int q = ( p + 2 ) % 3;
    int i = 0;

    for ( i = 0; i < 64; i++ ) {
        n = dns_cache[q][i];

        while ( n ) {
            m = n;
            n = n->next;
            free ( m );
        }

        dns_cache[q][i] = NULL;
    }

    /// end
    int nlen = strlen ( name );
    uint32_t key1 = fnv1a_32 ( name, nlen );
    uint32_t key2 = fnv1a_64 ( name, nlen );
    n = dns_cache[p][key1 % 64];

    while ( n != NULL ) {
        if ( n->key1 == key1 && n->key2 == key2 ) {
            break;
        }

        n = ( dns_cache_item_t * ) n->next;
    }

    if ( n ) {
        memcpy ( addr, &n->addr, sizeof ( struct in_addr ) );

        if ( n->recached != 1 ) {
            n->recached = 1;
            add_dns_cache ( name, n->addr, 1 );
        }

        return 1;
    }

    return 0;
}

void add_dns_cache ( const char *name, struct in_addr addr, int do_recache )
{
    time ( &timer );
    int p = ( timer / dns_cache_ttl ) % 3;

    if ( do_recache == 1 ) {
        p = ( p + 1 ) % 3;
    }

    dns_cache_item_t *n = NULL,
                      *m = NULL;
    int nlen = strlen ( name );
    uint32_t key1 = fnv1a_32 ( name, nlen );
    uint32_t key2 = fnv1a_64 ( name, nlen );
    int k = key1 % 64;
    n = dns_cache[p][k];

    if ( n == NULL ) {
        m = malloc ( sizeof ( dns_cache_item_t ) );

        if ( m == NULL ) {
            return;
        }

        m->key1 = key1;
        m->key2 = key2;
        m->next = NULL;
        m->recached = do_recache;
        memcpy ( &m->addr, &addr, sizeof ( struct in_addr ) );
        dns_cache[p][k] = m;

    } else {
        while ( n != NULL ) {
            if ( n->key1 == key1 && n->key2 == key2 ) {
                return; /// exists
            }

            if ( n->next == NULL ) { /// last
                m = malloc ( sizeof ( dns_cache_item_t ) );

                if ( m == NULL ) {
                    return;
                }

                m->key1 = key1;
                m->key2 = key2;
                m->next = NULL;
                m->recached = do_recache;
                memcpy ( &m->addr, &addr, sizeof ( struct in_addr ) );
                n->next = m;
                return;
            }

            n = ( dns_cache_item_t * ) n->next;
        }
    }
}

uint16_t dns_tid = 0;
struct sockaddr_in dns_servers[4];
int dns_server_count = 0;
char pkt[2048];

int be_get_dns_result ( se_ptr_t *ptr )
{
    cosocket_t *cok = ptr->data;
    int epoll_fd = ptr->epoll_fd;

    int len = 0;

    while ( ( len = recvfrom ( cok->dns_query_fd, pkt, 2048, 0, NULL, NULL ) ) > 0
            && len >= sizeof ( dns_query_header_t ) ) {
        int fd = ptr->fd;
        se_delete ( ptr );
        close ( fd );
        cok->ptr = NULL;

        const unsigned char *p = NULL,
                             *e = NULL,
                              *s = NULL;
        dns_query_header_t *header = NULL;
        uint16_t type = 0;
        int found = 0, stop = 0, dlen = 0, nlen = 0, i = 0;
        int err = 0;
        header = ( dns_query_header_t * ) pkt;

        if ( ntohs ( header->nqueries ) != 1 ) {
            err = 1;
        }

        if ( header->tid != cok->dns_tid ) {
            err = 1;
        }

        /* Skip host name */
        if ( err == 0 ) {
            for ( e = pkt + len, nlen = 0, s = p = &header->data[0]; p < e && *p != '\0'; p++ ) {
                nlen++;
            }
        }

        /* We sent query class 1, query type 1 */
        if ( &p[5] > e || NTOHS ( p + 1 ) != 0x01 ) {
            err = 1;
        }

        struct in_addr ips[10];

        /* Go to the first answer section */
        if ( err == 0 ) {
            p += 5;

            /* Loop through the answers, we want A type answer */
            for ( found = stop = 0; !stop && &p[12] < e; ) {
                /* Skip possible name in CNAME answer */
                if ( *p != 0xc0 ) {
                    while ( *p && &p[12] < e ) {
                        p++;
                    }

                    p--;
                }

                type = htons ( ( ( uint16_t * ) p ) [1] );

                if ( type == 5 ) {
                    /* CNAME answer. shift to the next section */
                    dlen = htons ( ( ( uint16_t * ) p ) [5] );
                    p += 12 + dlen;

                } else if ( type == 0x01 ) {
                    dlen = htons ( ( ( uint16_t * ) p ) [5] );
                    p += 12;

                    if ( p + dlen <= e ) {
                        memcpy ( &ips[found], p, dlen );
                    }

                    p += dlen;

                    if ( ++found == header->nanswers ) {
                        stop = 1;
                    }

                    if ( found >= 10 ) {
                        break;
                    }

                } else {
                    stop = 1;
                }
            }
        }

        if ( found > 0 ) {
            cok->addr.sin_addr = ips[cok->dns_query_fd % found];
            int sockfd = -1;

            add_dns_cache ( cok->dns_query_name, cok->addr.sin_addr, 0 );

            if ( cok->pool_size > 0 ) {
                cok->ptr = get_connection_in_pool ( epoll_fd, cok->pool_key );
            }

            int ret = -1;

            if ( !cok->ptr ) {
                if ( ( sockfd = socket ( AF_INET, SOCK_STREAM, 0 ) ) < 0 ) {
                    lua_pushnil ( cok->L );
                    lua_pushstring ( cok->L, "Init socket error!1" );
                    cok->inuse = 0;
                    int ret = lua_resume ( cok->L, 2 );

                    if ( ret == LUA_ERRRUN && lua_isstring ( cok->L, -1 ) ) {
                        printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( cok->L, -1 ) );
                        lua_pop ( cok->L, -1 );
                    }

                    return;
                }

                cok->fd = sockfd;

                if ( !coevent_setnonblocking ( cok->fd ) ) {
                    close ( cok->fd );
                    lua_pushnil ( cok->L );
                    lua_pushstring ( cok->L, "Init socket error!2" );
                    cok->inuse = 0;
                    int ret = lua_resume ( cok->L, 2 );

                    if ( ret == LUA_ERRRUN && lua_isstring ( cok->L, -1 ) ) {
                        printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( cok->L, -1 ) );
                        lua_pop ( cok->L, -1 );
                    }

                    return;
                }

                cok->reusedtimes = 0;

                connection_pool_counter_operate ( cok->pool_key, 1 );

                ret = connect ( cok->fd, ( struct sockaddr * ) &cok->addr,
                                sizeof ( struct sockaddr ) );
                cok->fd = sockfd;
                cok->ptr = se_add ( epoll_fd, sockfd, cok );

            } else {
                ( ( se_ptr_t * ) cok->ptr )->data = cok;
                cok->reusedtimes = 1;
                cok->fd = ( ( se_ptr_t * ) cok->ptr )->fd;
            }




            if ( cok->reusedtimes == 0 ) {
                se_be_write ( cok->ptr, cosocket_be_connected );
            }


            if ( ret == 0 || cok->reusedtimes > 0 ) {
                ///////// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! connected /////
                lua_pushboolean ( cok->L, 1 );
                cok->inuse = 0;
                int ret = lua_resume ( cok->L, 1 );

                if ( ret == LUA_ERRRUN && lua_isstring ( cok->L, -1 ) ) {
                    printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( cok->L, -1 ) );
                    lua_pop ( cok->L, -1 );
                }
            }

            return;
        }

        {
            cok->fd = -1;
            cok->status = 0;
            lua_pushnil ( cok->L );
            lua_pushstring ( cok->L, "names lookup error!" );
            cok->inuse = 0;
            int ret = lua_resume ( cok->L, 2 );

            if ( ret == LUA_ERRRUN && lua_isstring ( cok->L, -1 ) ) {
                printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( cok->L, -1 ) );
                lua_pop ( cok->L, -1 );
            }
        }

        break;
    }

    return 0;
}

int do_dns_query ( int epoll_fd, cosocket_t *cok, const char *name )
{
    if ( dns_server_count == 0 ) { /// init dns servers
        int p1 = 0,
            p2 = 0,
            p3 = 0,
            p4 = 0,
            i = 0;

        for ( i = 0; i < 4; i++ ) {
            dns_servers[i].sin_family = AF_INET;
            dns_servers[i].sin_port = htons ( 53 );
        }

        FILE *fp = NULL;
        char line[200],
             *p = NULL;

        if ( ( fp = fopen ( "/etc/resolv.conf" , "r" ) ) == NULL ) {
            printf ( "Failed opening /etc/resolv.conf file \n" );

        } else {
            while ( fgets ( line , 200 , fp ) ) {
                if ( line[0] == '#' ) {
                    continue;
                }

                if ( strncmp ( line , "nameserver" , 10 ) == 0 ) {
                    p = strtok ( line , " " );
                    p = strtok ( NULL , " " );

                    //p now is the dns ip :)
                    if ( sscanf ( p, "%d.%d.%d.%d", &p1, &p2, &p3, &p4 ) == 4 ) {
                        dns_servers[dns_server_count].sin_addr.s_addr = htonl ( ( ( p1 << 24 ) |
                                ( p2 << 16 ) | ( p3 << 8 ) | ( p4 ) ) );
                        dns_server_count ++;
                    }

                    if ( dns_server_count > 1 ) {
                        break;
                    }
                }
            }

            fclose ( fp );
        }

        if ( dns_server_count < 2 ) {
            dns_servers[dns_server_count].sin_addr.s_addr = inet_addr ( "8.8.8.8" );
            dns_server_count++;
        }

        if ( dns_server_count < 2 ) {
            dns_servers[dns_server_count].sin_addr.s_addr = inet_addr ( "208.67.22.222" );
            dns_server_count++;
        }
    }

    if ( ++dns_tid > 65535 - 1 ) {
        dns_tid = 1;
    }

    struct epoll_event ev;

    cok->dns_query_fd = socket ( PF_INET, SOCK_DGRAM, 17 );

    if ( cok->dns_query_fd < 0 ) {
        return 0;
    }

    cok->dns_tid = dns_tid;
    int nlen = strlen ( name );

    if ( nlen < 60 ) {
        memcpy ( cok->dns_query_name, name, nlen );
        cok->dns_query_name[nlen] = '\0';

    } else {
        cok->dns_query_name[0] = '-';
        cok->dns_query_name[1] = '\0';
        nlen = 1;
    }

    name = cok->dns_query_name;
    int opt = 1;
    ioctl ( cok->dns_query_fd, FIONBIO, &opt );

    int i = 0, n = 0, m = 0;
    dns_query_header_t *header = NULL;
    const char *s;
    char *p;
    header           = ( dns_query_header_t * ) pkt;
    header->tid      = dns_tid;
    header->flags    = htons ( 0x100 );
    header->nqueries = htons ( 1 );
    header->nanswers = 0;
    header->nauth    = 0;
    header->nother   = 0;
    // Encode DNS name
    p = ( char * ) &header->data; /* For encoding host name into packet */

    do {
        if ( ( s = strchr ( name, '.' ) ) == NULL ) {
            s = name + nlen;
        }

        n = s - name;           /* Chunk length */
        *p++ = n;               /* Copy length */

        for ( i = 0; i < n; i++ ) { /* Copy chunk */
            *p++ = name[i];
        }

        if ( *s == '.' ) {
            n++;
        }

        name += n;
        nlen -= n;
    } while ( *s != '\0' );

    *p++ = 0;           /* Mark end of host name */
    *p++ = 0;           /* Well, lets put this byte as well */
    *p++ = 1;           /* Query Type */
    *p++ = 0;
    *p++ = 1;           /* Class: inet, 0x0001 */
    n = p - pkt;        /* Total packet length */

    cok->ptr = se_add ( epoll_fd, cok->dns_query_fd, cok );
    se_be_read ( cok->ptr, be_get_dns_result );

    sendto (
        cok->dns_query_fd,
        pkt,
        n,
        0,
        ( struct sockaddr * ) &dns_servers[ ( cok->dns_query_fd + 1 ) % dns_server_count],
        sizeof ( struct sockaddr )
    );

    if ( ( m = sendto (
                   cok->dns_query_fd,
                   pkt,
                   n,
                   0,
                   ( struct sockaddr * ) &dns_servers[cok->dns_query_fd % dns_server_count],
                   sizeof ( struct sockaddr )
               )
         ) != n ) {
        return 0;
    }

    return 1;
}
