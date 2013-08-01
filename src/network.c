#include "main.h"
#include "network.h"
#include "timeouts.h"

extern FILE *LOG_FD;
extern int server_fd;

static char temp_buf[8192];

static struct iovec send_iov[_MAX_IOV_COUNT];
static int send_iov_count = 0;

int network_send_header ( epdata_t *epd, const char *header )
{
    if ( epd->process_timeout == 1 ) {
        return;
    }

    if ( !header ) {
        return 0;
    }

    int len = strlen ( header );

    if ( len < 1 || epd->response_header_length + len + 2 > 4096 ) {
        return 0;
    }

    if ( epd->iov[0].iov_base == NULL ) {
        epd->iov[0].iov_base = malloc ( 4096 );

        if ( epd->iov[0].iov_base == NULL ) {
            return 0;
        }

        if ( header[0] != 'H' && header[1] != 'T' ) {
            memcpy ( epd->iov[0].iov_base, "HTTP/1.1 200 OK\r\n", 17 );
            epd->response_header_length += 17;
        }
    }

    memcpy ( epd->iov[0].iov_base + epd->response_header_length, header, len );
    epd->response_header_length += len;

    if ( ( ( char * ) epd->iov[0].iov_base ) [epd->response_header_length - 1] != '\n' ) {
        memcpy ( epd->iov[0].iov_base + epd->response_header_length, "\r\n", 2 );
    }

    epd->response_header_length += 2;
    return 1;
}

int network_send ( epdata_t *epd, const char *data, int _len )
{
    if ( epd->process_timeout == 1 ) {
        return;
    }

    if ( _len < 1 || epd->response_sendfile_fd > -1 ) {
        return 0;
    }

    if ( epd->iov[1].iov_base == NULL ) {
        epd->iov[1].iov_base = malloc ( EP_D_BUF_SIZE );

        if ( epd->iov[1].iov_base == NULL ) {
            return 0;
        }

        epd->iov_buf_count = 1;
        epd->response_content_length = 0;
        epd->iov[1].iov_len = 0;
    }

    int len = _len;

    if ( epd->response_content_length + len > epd->iov_buf_count * EP_D_BUF_SIZE ) {
        len = epd->iov_buf_count * EP_D_BUF_SIZE - epd->response_content_length;
    }

    if ( len > 0 ) {
        memcpy ( epd->iov[epd->iov_buf_count].iov_base + ( epd->response_content_length %
                 EP_D_BUF_SIZE ), data, len );
        epd->iov[epd->iov_buf_count].iov_len += len;
        epd->response_content_length += len;
        _len -= len;
    }

    if ( _len > 0 ) {
        epd->iov[epd->iov_buf_count + 1].iov_base = malloc ( EP_D_BUF_SIZE );

        if ( epd->iov[epd->iov_buf_count + 1].iov_base == NULL ) {
            return 0;
        }

        epd->iov[epd->iov_buf_count + 1].iov_len = 0;
        epd->iov_buf_count += 1;

        if ( epd->iov_buf_count + 1 < _MAX_IOV_COUNT ) {
            return network_send ( epd, data + len, _len );
        }
    }

    return 1;
}

void free_epd ( epdata_t *epd )
{
    if ( !epd ) {
        return;
    }

    if ( epd->headers ) {
        free ( epd->headers );
    }

    int i = 0;

    for ( i = 0; i < epd->iov_buf_count && i < _MAX_IOV_COUNT; i++ ) {
        free ( epd->iov[i].iov_base );
        epd->iov[i].iov_base = NULL;
    }

    free ( epd );
}

void free_epd_request ( epdata_t *epd )  /// for keepalive
{
    if ( !epd ) {
        return ;
    }

    if ( epd->headers ) {
        free ( epd->headers );
        epd->headers = NULL;
    }

    int i = 0;

    for ( i = 0; i < epd->iov_buf_count; i++ ) {
        free ( epd->iov[i].iov_base );
        epd->iov[i].iov_base = NULL;
    }

    epd->contents = NULL;
    epd->data_len = 0;
    epd->header_len = 0;
    epd->_header_length = 0;
    epd->content_length = -1;
    epd->process_timeout = 0;
    epd->iov_buf_count = 0;

    se_be_read ( epd->se_ptr, network_be_read );
}

void close_client ( epdata_t *epd )
{
    if ( !epd ) {
        return;
    }

    if ( epd->status == STEP_READ ) {
        epoll_status.reading_counts--;

    } else if ( epd->status == STEP_SEND ) {
        epoll_status.sending_counts--;
    }
    
    se_delete ( epd->se_ptr );
    delete_timeout ( epd->timeout_ptr );
    epd->timeout_ptr = NULL;

    if ( epd->fd > -1 ) {
        epoll_status.active_counts--;
        close ( epd->fd );
        epd->fd = -1;
    }

    free_epd ( epd );
}

void network_end_process ( epdata_t *epd )
{
    epd->status = STEP_WAIT;

    int i = 0;
    int response_code = 0;

    if ( epd->iov[0].iov_base ) {
        char *hl = strtok ( epd->iov[0].iov_base, "\n" );

        if ( hl ) {
            hl = strtok ( hl, " " );
            hl = strtok ( NULL, " " );

            if ( hl ) {
                response_code = atoi ( hl );
            }
        }
    }

    for ( i = 0; i < epd->iov_buf_count; i++ ) {
        free ( epd->iov[i].iov_base );
        epd->iov[i].iov_base = NULL;
        epd->iov[i].iov_len = 0;
    }

    long ttime = longtime();

    if ( LOG_FD ) fprintf ( LOG_FD,
                                "%s - - [%s] %s \"%s %s%s%s %s\" %d %d \"%s\" \"%s\" %.3f\n",
                                inet_ntoa ( epd->client_addr ),
                                now_date,
                                epd->host ? epd->host : "-",
                                epd->method ? epd->method : "-",
                                epd->uri ? epd->uri : "/",
                                epd->query ? "?" : "",
                                epd->query ? epd->query : "",
                                epd->http_ver ? epd->http_ver : "-",
                                response_code,
                                epd->response_content_length - epd->response_header_length,
                                epd->referer ? epd->referer : "-",
                                epd->user_agent ? epd->user_agent : "-",
                                ( float ) ( ttime - epd->start_time ) / 1000 );

    epd->response_header_length = 0;
    epd->response_content_length = 0;
    epd->iov_buf_count = 0;
    epd->response_buf_sended = 0;

    if ( epd->keepalive == 1 ) {
        update_timeout ( epd->timeout_ptr, STEP_WAIT_TIMEOUT );
        free_epd_request ( epd );

    } else {
        close_client ( epd );
        return;
    }
}

void network_be_end ( epdata_t *epd )  // for lua function die
{
    if ( epd->process_timeout == 1 && epd->keepalive != -1 ) {
        epd->process_timeout = 0;
        free_epd ( epd );
        return;
    }

    //printf ( "network_be_end %d\n" , ((se_ptr_t*)epd->se_ptr)->fd );
    epoll_status.success_counts++;
    update_timeout ( epd->timeout_ptr, STEP_SEND_TIMEOUT );
    se_be_write ( epd->se_ptr, network_be_write );
    epd->status = STEP_SEND;

    if ( epd->iov[0].iov_base == NULL && epd->iov[1].iov_base == NULL
         && epd->response_sendfile_fd == -1 ) {
        network_send_error ( epd, 417, "" );

    } else if ( epd->response_sendfile_fd == -2 ) {
        epd->response_sendfile_fd = -1;
        network_send_error ( epd, 404, "File Not Found!" );

    } else {
        epoll_status.sending_counts++;
        int gzip_data = 0;

        //printf("%d %s\n", epd->response_content_length, epd->iov[1].iov_base);
        if ( epd->response_content_length > 1024 && epd->iov[1].iov_base &&
             !is_binary ( epd->iov[1].iov_base, epd->iov[1].iov_len )
           ) {
            char *p = NULL;

            if ( epd->headers ) {
                p = stristr ( epd->headers, "Accept-Encoding", epd->header_len );
            }

            if ( p ) {
                p += 16;
                int i = 0, m = strlen ( p );

                for ( ; i < 20 && i < m; i++ ) {
                    if ( p[i] == '\n' ) {
                        break;
                    }
                }

                p[i] = '\0';

                if ( strstr ( p, "deflate" ) ) {
                    gzip_data = 2;

                } else if ( strstr ( p, "gzip" ) ) {
                    gzip_data = 1;
                }

                p[i] = '\n';
            }
        }

        if ( gzip_data > 0 )
            epd->response_content_length = gzip_iov ( gzip_data,
                                           ( struct iovec * ) &epd->iov,
                                           epd->iov_buf_count,
                                           &epd->iov_buf_count );

        int len = 0;

        if ( epd->iov[0].iov_base == NULL ) {
            len = sprintf ( temp_buf,
                            "HTTP/1.1 200 OK\r\nServer: aLiLua/%s (%s)\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: %s\r\n%sContent-Length: %d\r\n\r\n",
                            version, hostname, ( epd->keepalive == 1 ? "keep-alive" : "close" ),
                            ( gzip_data == 1 ? "Content-Encoding: gzip\r\n" : ( gzip_data == 2 ?
                                    "Content-Encoding: deflate\r\n" : "" ) ),
                            epd->response_content_length + ( gzip_data == 1 ? 10 : 0 ) );
            epd->response_header_length = len;

        } else {
            ( ( char * ) ( epd->iov[0].iov_base ) ) [epd->response_header_length] = '\0';
            memcpy ( temp_buf, epd->iov[0].iov_base, epd->response_header_length );
            len = epd->response_header_length + sprintf ( temp_buf + epd->response_header_length,
                    "Server: aLiLua/%s (%s)\r\nConnection: %s\r\nDate: %s\r\n%sContent-Length: %d\r\n\r\n",
                    version, hostname, ( epd->keepalive == 1 ? "keep-alive" : "close" ), now_date,
                    ( gzip_data == 1 ? "Content-Encoding: gzip\r\n" : ( gzip_data == 2 ?
                            "Content-Encoding: deflate\r\n" : "" ) ),
                    epd->response_content_length + ( gzip_data == 1 ? 10 : 0 ) );
            epd->response_header_length += len;
        }

        if ( len < 4086 && epd->response_sendfile_fd <= -1 && epd->iov[1].iov_base
             && epd->iov[1].iov_len > 0 ) {
            if ( epd->iov[0].iov_base == NULL ) {
                epd->iov[0].iov_base = malloc ( 4096 );
            }

            if ( epd->iov[0].iov_base == NULL ) {
                epd->keepalive = 0;
                network_end_process ( epd );
                epoll_status.sending_counts--;
                return;
            }

            memcpy ( epd->iov[0].iov_base, temp_buf, len );
            epd->iov[0].iov_len = len;
            epd->response_content_length += len;

            if ( gzip_data == 1 ) {
                memcpy ( epd->iov[0].iov_base + len, gzip_header, 10 );
                epd->iov[0].iov_len += 10;
                epd->response_content_length += 10;
            }

            epd->iov_buf_count += 1;

        } else {
            network_raw_send ( epd->fd, temp_buf, len );

            if ( gzip_data == 1 ) {
                network_raw_send ( epd->fd, gzip_header, 10 );
            }

            free ( epd->iov[0].iov_base );
            epd->iov[0].iov_base = NULL;
            int i = 0;

            for ( i = 0; i < epd->iov_buf_count; i++ ) {
                epd->iov[i] = epd->iov[i + 1];
                epd->iov[i + 1].iov_base = NULL;
                epd->iov[i + 1].iov_len = 0;
            }
        }

        //epd->response_header_length = 0;
        //printf("%d\n", epd->response_header_length);

        if ( epd->response_content_length == 0 ) {
            network_end_process ( epd );
            epoll_status.sending_counts--;

        } else {
            epd->response_buf_sended = 0;

        }
    }
}

static void timeout_handle ( void *ptr )
{
    //printf ( "timeout_handle\n" );
    epdata_t *epd = ptr;

    if ( epd->status == STEP_READ ) {
        epoll_status.reading_counts--;

    } else if ( epd->status == STEP_SEND ) {
        epoll_status.sending_counts--;
    }

    epd->timeout_ptr = NULL;

    close_client ( epd );
}

static int network_be_read ( se_ptr_t *ptr )
{
    epdata_t *epd = ptr->data;

    if ( !epd ) {
        return 0;
    }

    int n = 0;

    update_timeout ( epd->timeout_ptr, STEP_READ_TIMEOUT );

    if ( epd->headers == NULL ) {
        epd->headers = malloc ( 4096 );

        if ( epd->headers == NULL ) {
            epd->status = STEP_FINISH;
            network_send_error ( epd, 503, "memory error!" );
            close_client ( epd );
            epoll_status.reading_counts--;
            return 0;
        }

        epd->buf_size = 4096;

    } else if ( epd->data_len + 4096 > epd->buf_size ) {
        char *_t = ( char * ) realloc ( epd->headers, epd->buf_size + 4096 );

        if ( _t != NULL ) {
            epd->headers = _t;

        } else {
            network_send_error ( epd, 503, "memory error!" );
            close_client ( epd );
            epoll_status.reading_counts--;
            return 0;
        }

        epd->buf_size += 4096;
    }

    while ( ( n = recv ( epd->fd, epd->headers + epd->data_len, 4096, 0 ) ) >= 0 ) {
        if ( n == 0 ) {
            close_client ( epd );
            epd = NULL;
            break;
        }

        if ( epd->data_len + n + 4096 > epd->buf_size ) {
            char *_t = ( char * ) realloc ( epd->headers, epd->buf_size + 4096 );

            if ( _t != NULL ) {
                epd->headers = _t;

            } else {
                network_send_error ( epd, 503, "buf error!" );
                close_client ( epd );
                epd = NULL;
                epoll_status.reading_counts--;
                break;
            }

            epd->buf_size += 4096;
        }

        if ( epd->status != STEP_READ ) {
            epoll_status.reading_counts++;
            epd->status = STEP_READ;
            epd->data_len = n;
            epd->start_time = longtime();

        } else {
            epd->data_len += n;
        }

        if ( epd->_header_length < 1 && epd->data_len >= 4 && epd->headers ) {
            int _get_content_length = 0;

            if ( epd->headers[epd->data_len - 1] == '\n' &&
                 ( epd->headers[epd->data_len - 2] == '\n' ||
                   ( epd->headers[epd->data_len - 4] == '\r' &&
                     epd->headers[epd->data_len - 2] == '\r' ) ) ) {
                epd->_header_length = epd->data_len;

            } else {
                _get_content_length = 1;
                char *fp2 = stristr ( epd->headers, "\r\n\r\n", epd->data_len );

                if ( fp2 ) {
                    epd->_header_length = ( fp2 - epd->headers ) + 4;

                } else {
                    fp2 = stristr ( epd->headers, "\n\n", epd->data_len );

                    if ( fp2 ) {
                        epd->_header_length = ( fp2 - epd->headers ) + 2;
                    }
                }
            }

            if ( epd->_header_length > 0 && epd->content_length < 0 ) {
                /// not POST or PUT request
                if ( _get_content_length == 0 && epd->headers[0] != 'P' && epd->headers[0] != 'p' ) {
                    epd->content_length = 0;

                } else {
                    int flag = 0;

                    char *fp = stristr ( epd->headers, "\ncontent-length:", epd->data_len );

                    if ( fp ) {
                        int fp_at = fp - epd->headers + 16;
                        int i = 0, _oc;

                        for ( i = fp_at; i < epd->data_len; i++ ) {
                            if ( epd->headers[i] == '\r' || epd->headers[i] == '\n' ) {
                                flag = 1;
                                fp = epd->headers + fp_at;
                                _oc = epd->headers[i];
                                epd->headers[i] = '\0';
                                break;
                            }
                        }

                        if ( flag ) {
                            epd->content_length = atoi ( fp );
                            epd->headers[i] = _oc;
                        }

                        if ( stristr ( epd->headers + ( epd->_header_length - 60 ), "100-continue",
                                       epd->_header_length ) ) {
                            network_raw_send ( epd->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25 );
                        }
                    }
                }
            }

            if ( epd->_header_length > 0
                 && epd->_header_length < epd->data_len
                 && epd->content_length < 1 ) {

                network_send_error ( epd, 411, "" );
                close_client ( epd );
                epd = NULL;
                epoll_status.reading_counts--;

                break;
            }
        }

        //printf("data_len = %d header_length = %d content_length = %d\n", epd->data_len, epd->_header_length, epd->content_length);

        if ( epd->_header_length > 0 && ( ( epd->content_length < 1
                                            && epd->_header_length > 0 ) ||
                                          epd->content_length <= epd->data_len - epd->_header_length ) ) {


            /// start job
            epd->header_len = epd->_header_length;
            epd->headers[epd->data_len] = '\0';
            epd->header_len -= 1;
            epd->headers[epd->header_len] = '\0';

            if ( epd->header_len + 1 < epd->data_len ) {
                epd->contents = epd->headers + epd->header_len + 1;

            } else {
                epd->content_length = 0;
            }


            if ( USE_KEEPALIVE == 1 && ( epd->keepalive == 1 ||
                                         ( stristr ( epd->headers, "keep-alive", epd->header_len ) ) )
               ) {
                epd->keepalive = 1;
            }

            epd->response_header_length = 0;
            epd->iov_buf_count = 0;
            epd->response_content_length = 0;

            epd->response_sendfile_fd = -1;

            epd->iov[0].iov_base = NULL;
            epd->iov[1].iov_base = NULL;
            epd->iov[1].iov_len = 0;

            /// output server status !!!!!!!!!!
            {
                int i, len;
                char *uri = NULL;
                uri = epd->headers;

                for ( i = 0; i < epd->header_len; i++ )
                    if ( uri[i] == ' ' ) {
                        break;
                    }

                for ( ; i < epd->header_len; i++ )
                    if ( uri[i] != ' ' ) {
                        break;
                    }

                uri = epd->headers + i;
                len = strlen ( uri );

                for ( i = 0; i < len; i++ ) {
                    if ( uri[i] == '\r' || uri[i] == '\n' || uri[i] == ' ' ) {
                        break;
                    }
                }

                if ( i > 11 && strncmp ( "/serv-status", uri, i ) == 0 ) {
                    epd->process_timeout = 0;
                    network_send_status ( epd );
                    epoll_status.reading_counts--;
                    break;
                }
            }
            /// end.

            se_be_pri ( epd->se_ptr, NULL ); // be wait

            if ( epd->status == STEP_READ ) {
                epoll_status.reading_counts--;
                epd->status = STEP_PROCESS;

                epoll_status.sec_process_counts[ ( now ) % 5]++;
                epoll_status.process_counts++;
                epd->method = NULL;
                epd->uri = NULL;
                epd->host = NULL;
                epd->query = NULL;
                epd->http_ver = NULL;
                epd->referer = NULL;
                epd->user_agent = NULL;

                if ( process_func ( epd, 0 ) != 0 ) {
                    close_client ( epd );
                    epd = NULL;
                }
            }

            break;
        }
    }

    if ( epd && n < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) {
        //printf("error fd %d (%d) %s\n", epd->fd, errno, strerror(errno));
        close_client ( epd );
        epd = NULL;
        return 0;
    }

    return 1;
}

static int network_be_write ( se_ptr_t *ptr )
{
    epdata_t *epd = ptr->data;

    if ( !epd ) {
        return 0;
    }

    int n = 0;
    send_iov_count = 0;

    update_timeout ( epd->timeout_ptr, STEP_SEND_TIMEOUT );
    //printf ( "network_be_write\n" );

    if ( epd->status == STEP_SEND ) {
        int j = 0, k = 0;
        n = 0;

        while ( epd->response_buf_sended < epd->response_content_length && n >= 0 ) {
            if ( epd->response_sendfile_fd == -1 ) {
                epd->response_buf_sended += n;
                {
                    int all = 0;
                    send_iov_count = 0;
                    size_t sended = 0;

                    for ( j = 0; j < epd->iov_buf_count; j++ ) {
                        sended += epd->iov[j].iov_len;

                        if ( sended > epd->response_buf_sended ) {
                            if ( k == 0 && epd->response_buf_sended > 0 ) {
                                k = epd->response_buf_sended - ( sended - epd->iov[j].iov_len );
                            }

                            send_iov[send_iov_count] = epd->iov[j];
                            send_iov_count++;
                        }

                    }

                    send_iov[0].iov_base += k;
                    send_iov[0].iov_len -= k;
                    send_iov[send_iov_count + 1].iov_base = NULL;
                    n = writev ( epd->fd, send_iov, send_iov_count );
                }

                if ( epd->response_buf_sended + n >= epd->response_content_length ) {
                    //printf("%ld sended %ld\n", epd->response_content_length, epd->response_buf_sended+n);
                    network_end_process ( epd );
                    epoll_status.sending_counts--;
                    break;
                }

            } else {
                n = sendfile ( epd->fd, epd->response_sendfile_fd, &epd->response_buf_sended,
                               epd->response_content_length );

                if ( epd->response_buf_sended >= epd->response_content_length ) {
                    close ( epd->response_sendfile_fd );
                    epd->response_sendfile_fd = -1;
                    int set = 0;
                    setsockopt ( epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof ( int ) );
                    network_end_process ( epd );
                    epoll_status.sending_counts--;
                    break;
                }
            }

            if ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) {
                //printf("error end\n");
                epd->keepalive = 0;

                if ( epd->response_sendfile_fd > -1 ) {
                    int set = 0;
                    setsockopt ( epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof ( int ) );
                    close ( epd->response_sendfile_fd );
                }

                network_end_process ( epd );
                epoll_status.sending_counts--;
                break;
            }
        }
    }
}

static int network_be_accept ( se_ptr_t *ptr )
{
    epdata_t *epd = NULL;
    int acc_trys = 0, client_fd = -1;
    struct sockaddr_in remote_addr;
    int addr_len = 0;

    while ( acc_trys++ < 2 ) {
        client_fd = accept ( server_fd, ( struct sockaddr * ) &remote_addr, &addr_len );

        if ( client_fd < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
            continue;

        }

        if ( !setnonblocking ( client_fd ) ) {
            close ( client_fd );
            return 0;
        }

        epd = malloc ( sizeof ( epdata_t ) );

        if ( !epd ) {
            close ( client_fd );
            return 0;
        }

        epd->fd = client_fd;
        epd->client_addr = remote_addr.sin_addr;
        epd->stime = now;
        epd->status = STEP_WAIT;
        epd->headers = NULL;
        epd->header_len = 0;
        epd->contents = NULL;
        epd->data_len = 0;
        epd->content_length = -1;
        epd->_header_length = 0;
        epd->keepalive = -1;
        epd->process_timeout = 0;
        epd->iov_buf_count = 0;

        epd->se_ptr = se_add ( epoll_fd, client_fd, epd );
        epd->timeout_ptr = add_timeout ( epd, STEP_WAIT_TIMEOUT, timeout_handle );

        se_be_read ( epd->se_ptr, network_be_read );

        epoll_status.active_counts++;
        epoll_status.connect_counts++;

        acc_trys = 0;
    }
}

static int without_jobs()
{
    check_timeouts();

    if ( checkProcessForExit() || has_error_for_exit ) {
        //network_send_error ( epd, 500, "Child Process restart!" );
        //close_client ( epd );
    }

    sync_epoll_status();

    do_other_jobs();
}

void network_worker ( void *_process_func, int process_count )
{
    signal ( SIGPIPE, SIG_IGN );

    _shm_epoll_status = shm_malloc ( sizeof ( epoll_status_t ) );

    if ( _shm_epoll_status == NULL ) {
        perror ( "shm malloc failed\n" );
        signal ( SIGHUP, SIG_IGN );
        exit ( 1 );
    }

    shm_epoll_status = _shm_epoll_status->p;
    memcpy ( shm_epoll_status, &epoll_status, sizeof ( epoll_status_t ) );

    process_func = _process_func;

    init_mime_types();

    epoll_fd = se_create ( EPD_POOL_SIZE );
    set_epoll_fd ( epoll_fd, process_count );

    se_ptr_t *se = se_add ( epoll_fd, server_fd, NULL );
    se_be_read ( se, network_be_accept );

    while ( 1 ) {
        se_loop ( epoll_fd, EPOLL_WAITOUT, without_jobs );

        if ( checkProcessForExit() || has_error_for_exit ) {
            break;
        }
    }
}
