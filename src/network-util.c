#include "main.h"

static char temp_buf[8192];
static serv_status_t tmp_status;

static uint64_t time_micros ( void )
{
    struct timeval now;
    gettimeofday ( &now, NULL );
    return ( now.tv_sec * 1000000U + now.tv_usec );
}

int setnonblocking ( int fd )
{
    int opts;
    opts = fcntl ( fd, F_GETFL );

    if ( opts < 0 ) {
        perror ( "fcntl failed\n" );
        return 0;
    }

    opts = opts | O_NONBLOCK;

    if ( fcntl ( fd, F_SETFL, opts ) < 0 ) {
        perror ( "fcntl failed\n" );
        return 0;
    }

    return 1;
}


int network_bind ( char *addr, int port )
{
    int fd = -1;
    struct sockaddr_in sin;

    if ( ( fd = socket ( AF_INET, SOCK_STREAM, 0 ) ) <= 0 ) {
        perror ( "Socket failed" );
        signal ( SIGHUP, SIG_IGN );
        exit ( 1 );
    }

    int reuseaddr = 1, nodelay = 1;
    setsockopt ( fd, SOL_SOCKET, SO_REUSEADDR, ( const void * ) &reuseaddr,
                 sizeof ( int ) );
    setsockopt ( fd, IPPROTO_TCP, TCP_NODELAY, ( const void * ) &nodelay, sizeof ( int ) );

    memset ( &sin, 0, sizeof ( struct sockaddr_in ) );
    sin.sin_family = AF_INET;
    sin.sin_port = htons ( ( short ) ( port ) );

    if ( strlen ( addr ) > 6 ) {
        inet_aton ( addr, & ( sin.sin_addr ) );

    } else {
        sin.sin_addr.s_addr = INADDR_ANY;
    }

    if ( bind ( fd, ( struct sockaddr * ) &sin, sizeof ( sin ) ) != 0 ) {
        sleep ( 1 );

        if ( bind ( fd, ( struct sockaddr * ) &sin, sizeof ( sin ) ) != 0 ) {
            perror ( "bind failed\n" );
            signal ( SIGHUP, SIG_IGN );
            exit ( 1 );
        }
    }

    if ( listen ( fd, 32 ) != 0 ) {
        perror ( "listen failed\n" );
        signal ( SIGHUP, SIG_IGN );
        exit ( 1 );
    }

    if ( !setnonblocking ( fd ) ) {
        perror ( "set nonblocking failed\n" );
        exit ( 1 );
    }

    memset ( &tmp_status, 0, sizeof ( serv_status_t ) );

    return fd;
}

int network_raw_send ( int client_fd, const char *contents, int length )
{
    int len = 0, n;
    int a = 0;
    int max = length;

    while ( 1 ) {
        if ( len >= length || length < 1 ) {
            break;
        }

        n = send ( client_fd, contents + len, length - len, MSG_DONTWAIT );

        if ( n < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                if ( a++ > max ) {
                    return 0;
                }

                continue;

            } else {
                return -1;
                break;
            }
        }

        len += n;
    }

    return len;
}

char *network_raw_read ( int cfd, int *datas_len )
{
    char *datas = NULL;
    int len = 0;
    int n = 0;

    while ( 1 ) {
        if ( datas == NULL ) {
            datas = ( char * ) calloc ( 1, sizeof ( char ) * EP_D_BUF_SIZE );
            memset ( datas, 0, EP_D_BUF_SIZE );

        } else {
            datas = ( char * ) realloc ( datas, sizeof ( char ) * ( len + EP_D_BUF_SIZE ) );
            memset ( datas + len, 0, EP_D_BUF_SIZE );
        }

        if ( ( n = read ( cfd, datas + len, EP_D_BUF_SIZE ) ) <= 0 ) {
            if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
                continue;
            }

            break;
        }

        len += n;

        if ( datas[len - 3] == '\n' && datas[len - 1] == '\n' ) {
            break;
        }
    }

    if ( datas[len - 3] == '\n' && datas[len - 1] == '\n' ) {
        datas[len - 4] = '\0';

    } else {
        free ( datas );
        datas = NULL;
        len = 0;
    }

    if ( *datas_len ) {
        *datas_len = len;
    }

    return datas;
}


int network_raw_sendfile ( int out_fd, int in_fd, off_t *offset, size_t count )
{
#if defined(__APPLE__) || defined(__FreeBSD__)
    off_t my_count = count;
    int rc;

    // We have to do this loop nastiness, because mac os x fails with resource
    // temporarily unavailable (per bug e8eddb51a8)
    do {
#if defined(__APPLE__)
        rc = sendfile ( in_fd, out_fd, *offset, &my_count, NULL, 0 );
#elif defined(__FreeBSD__)
        rc = sendfile ( in_fd, out_fd, *offset, count, NULL, &my_count, 0 );
#endif
        *offset += my_count;
    } while ( rc != 0 && errno == 35 );

    return my_count;
#else
    return sendfile ( out_fd, in_fd, offset, count );
#endif
}

void network_send_error ( epdata_t *epd, int code, const char *msg )
{
    char *code_ = NULL;
    int len = strlen ( msg );

    if ( code < 300 ) {
        if ( code == 100 ) {
            code_ = "Continue";

        } else if ( code == 101 ) {
            code_ = "Switching Protocols";

        } else if ( code == 200 ) {
            code_ = "OK";

        } else if ( code == 201 ) {
            code_ = "Created";

        } else if ( code == 202 ) {
            code_ = "Accepted";

        } else if ( code == 203 ) {
            code_ = "Non-Authoritative Information";

        } else if ( code == 204 ) {
            code_ = "No Content";

        } else if ( code == 205 ) {
            code_ = "Reset Content";

        } else if ( code == 206 ) {
            code_ = "Partial Content";
        }

    } else if ( code < 400 ) {
        if ( code == 300 ) {
            code_ = "Multiple Choices";

        } else if ( code == 301 ) {
            code_ = "Moved Permanently";

        } else if ( code == 302 ) {
            code_ = "Found";

        } else if ( code == 303 ) {
            code_ = "See Other";

        } else if ( code == 304 ) {
            code_ = "Not Modified";

        } else if ( code == 305 ) {
            code_ = "Use Proxy";

        } else if ( code == 307 ) {
            code_ = "Temporary Redirect";
        }

    } else if ( code < 500 ) {
        if ( code == 400 ) {
            code_ = "Bad Request";

        } else if ( code == 401 ) {
            code_ = "Unauthorized";

        } else if ( code == 402 ) {
            code_ = "Payment Required";

        } else if ( code == 403 ) {
            code_ = "Forbidden";

        } else if ( code == 404 ) {
            code_ = "Not Found";

        } else if ( code == 405 ) {
            code_ = "Method Not Allowed";

        } else if ( code == 406 ) {
            code_ = "Not Acceptable";

        } else if ( code == 407 ) {
            code_ = "Proxy Authentication Required";

        } else if ( code == 408 ) {
            code_ = "Request Timeout";

        } else if ( code == 409 ) {
            code_ = "Conflict";

        } else if ( code == 410 ) {
            code_ = "Gone";

        } else if ( code == 411 ) {
            code_ = "Length Required";

        } else if ( code == 412 ) {
            code_ = "Precondition Failed";

        } else if ( code == 413 ) {
            code_ = "Request Entity Too Large";

        } else if ( code == 414 ) {
            code_ = "Request-URI Too Long";

        } else if ( code == 415 ) {
            code_ = "Unsupported Media Type";

        } else if ( code == 416 ) {
            code_ = "Requested Range Not Satisfiable";

        } else if ( code == 417 ) {
            code_ = "Expectation Failed";
        }

    } else {
        if ( code == 500 ) {
            code_ = "Internal Server Error";

        } else if ( code == 501 ) {
            code_ = "Not Implemented";

        } else if ( code == 502 ) {
            code_ = "Bad Gateway";

        } else if ( code == 503 ) {
            code_ = "Service Unavailable";

        } else if ( code == 504 ) {
            code_ = "Gateway Timeout";

        } else if ( code == 505 ) {
            code_ = "HTTP Version Not Supported";
        }
    }

    //sprintf(temp_buf,"HTTP/1.1 %d %s\r\nServer: (%s)\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %d\r\n\r\n<h1>%d %s</h1><p>%s</p>", code, code_, hostname, (int)(strlen(msg)+20+strlen(code_)), code, code_, msg);
    int len2 = sprintf ( temp_buf,
                         "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=UTF-8", code, code_, hostname );
    temp_buf[len2] = '\0';

    int e = epd->process_timeout;
    epd->process_timeout = 0;

    if ( code == 408 && epd->iov[0].iov_base ) { /// clear lua output
        epd->iov[0].iov_len = 0;
    }

    network_send_header ( epd, temp_buf );
    network_send ( epd, msg, len );
    epd->process_timeout = e;
    network_be_end ( epd );
}

void sync_serv_status()
{
    time_t now2;
    time ( &now2 );

    if ( now2 <= now ) {
        return;
    }

    now = now2;

    serv_status.sec_process_counts[ ( now - 5 ) % 5] = 0;

    int i, k = 0;

    gmtime_r ( &now, &now_tm );
    sprintf ( now_date, "%s, %02d %s %04d %02d:%02d:%02d GMT",
              DAYS_OF_WEEK[now_tm.tm_wday],
              now_tm.tm_mday,
              MONTHS_OF_YEAR[now_tm.tm_mon],
              now_tm.tm_year + 1900,
              now_tm.tm_hour,
              now_tm.tm_min,
              now_tm.tm_sec );


    shm_lock ( _shm_serv_status );
    shm_serv_status->connect_counts += serv_status.connect_counts;
    serv_status.connect_counts = 0;
    shm_serv_status->success_counts += serv_status.success_counts;
    serv_status.success_counts = 0;
    shm_serv_status->process_counts += serv_status.process_counts;
    serv_status.process_counts = 0;

    shm_serv_status->waiting_counts += serv_status.waiting_counts -
                                       tmp_status.waiting_counts;
    shm_serv_status->reading_counts += serv_status.reading_counts -
                                       tmp_status.reading_counts;
    shm_serv_status->sending_counts += serv_status.sending_counts -
                                       tmp_status.sending_counts;

    shm_serv_status->active_counts += serv_status.active_counts -
                                      tmp_status.active_counts;

    shm_serv_status->sec_process_counts[0] += serv_status.sec_process_counts[0] -
            tmp_status.sec_process_counts[0];
    shm_serv_status->sec_process_counts[1] += serv_status.sec_process_counts[1] -
            tmp_status.sec_process_counts[1];
    shm_serv_status->sec_process_counts[2] += serv_status.sec_process_counts[2] -
            tmp_status.sec_process_counts[2];
    shm_serv_status->sec_process_counts[3] += serv_status.sec_process_counts[3] -
            tmp_status.sec_process_counts[3];
    shm_serv_status->sec_process_counts[4] += serv_status.sec_process_counts[4] -
            tmp_status.sec_process_counts[4];

    memcpy ( &tmp_status, &serv_status, sizeof ( serv_status_t ) );

    shm_unlock ( _shm_serv_status );

}

void network_send_status ( epdata_t *epd )
{
    int len = sprintf ( temp_buf,
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8", hostname );

    network_send_header ( epd, temp_buf );

    shm_lock ( _shm_serv_status );
    int sum_cs = 0;
    int i = 0;

    for ( i = now - 3; i < now; i++ ) {
        sum_cs += shm_serv_status->sec_process_counts[i % 5];
    }

    len = sprintf ( temp_buf, "Active connections: %d\nserver accepts handled requests\n%"
                    PRIu64 " %" PRIu64 " %" PRIu64
                    "\nReading: %d Writing: %d Waiting: %d\nRequests per second: %d [#/sec] (mean)\n",
                    shm_serv_status->active_counts,
                    shm_serv_status->connect_counts, shm_serv_status->success_counts,
                    shm_serv_status->process_counts,
                    shm_serv_status->reading_counts, shm_serv_status->sending_counts,
                    shm_serv_status->active_counts - shm_serv_status->reading_counts -
                    shm_serv_status->sending_counts, ( sum_cs / 3 ) );

    shm_unlock ( _shm_serv_status );

    network_send ( epd, temp_buf, len );
    network_be_end ( epd );
}
