#include "main.h"
#include "config.h"
#include "network.h"
#include "websocket.h"
#include "worker.h"
#include "cached-ntoa.h"

#define BUFFER_SIZE 4096
static char temp_buf[8192];
static char temp_buf64k[61440];

extern logf_t *ACCESS_LOG;
extern int max_request_header;
extern int max_request_body;

static struct iovec send_iov[_MAX_IOV_COUNT + 1];
static int send_iov_count = 0;

static int SSL_raw_send(SSL *ssl, const char *contents, int length)
{
    int len = 0, n = 0;
    int a = 0;
    int max = length;

    while(1) {
        if(len >= length || length < 1) {
            break;
        }

        n = SSL_write(ssl, contents + len, length - len);

        if(n < 0) {

            int status = SSL_get_error(ssl, n);
            errno = (status == SSL_ERROR_WANT_READ || status == SSL_ERROR_WANT_WRITE) ? EAGAIN : EPIPE;

            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                if(a++ > max) {
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

int network_send_header(epdata_t *epd, const char *header)
{
    if(epd->process_timeout == 1) {
        return 0;
    }

    if(!header) {
        return 0;
    }

    int len = strlen(header);

    if(len < 1) {
        return 0;
    }

    if(epd->response_header_length + len + 200 > EP_D_BUF_SIZE) {
        return -1;
    }

    if(epd->iov[0].iov_base == NULL) {
        epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);

        if(epd->iov[0].iov_base == NULL) {
            LOGF(ERR, "malloc error");
            return 0;
        }

        if(header[0] != 'H' && header[4] != '/') {
            if((header[0] == 'L' || header[0] == 'l') && stricmp(header, "Location:")) {
                memcpy(epd->iov[0].iov_base, "HTTP/1.1 302 Moved\r\n", 20);
                epd->response_header_length += 20;

            } else {
                memcpy(epd->iov[0].iov_base, "HTTP/1.1 200 OK\r\n", 17);
                epd->response_header_length += 17;
            }
        }
    }

    if(header[0] == 'H' && header[4] == '/') {
        char *_ohs = epd->iov[0].iov_base;

        if(_ohs[0] == 'H' && _ohs[4] == '/') {
            int l = 0;

            for(l = 0; l < epd->response_header_length; l++) {
                if(_ohs[l] == '\n') {
                    l += 1;
                    memmove(epd->iov[0].iov_base + len + 2, epd->iov[0].iov_base + l, epd->response_header_length - l);
                    memcpy(epd->iov[0].iov_base, header, len);
                    memcpy(epd->iov[0].iov_base + len, "\r\n", 2);
                    epd->response_header_length -= l;
                    epd->response_header_length += len + 2;
                    return 1;
                }
            }
        }
    }

    memcpy(epd->iov[0].iov_base + epd->response_header_length, header, len);
    epd->response_header_length += len;

    if(((char *) epd->iov[0].iov_base) [epd->response_header_length - 1] != '\n') {
        memcpy(epd->iov[0].iov_base + epd->response_header_length, "\r\n", 2);
    }

    epd->response_header_length += 2;

    return 1;
}

int network_send(epdata_t *epd, const char *data, int _len)
{
    if(epd->process_timeout == 1) {
        return 0;
    }

    if(_len < 1 || epd->response_sendfile_fd > -1) {
        return -1;
    }

    if(epd->iov_buf_count + 1 >= _MAX_IOV_COUNT) {
        return _len;
    }

    if(epd->iov[1].iov_base == NULL) {
        epd->iov[1].iov_base = malloc(EP_D_BUF_SIZE);

        if(epd->iov[1].iov_base == NULL) {
            return -2;
        }

        epd->iov_buf_count = 1;
        epd->response_content_length = 0;
        epd->iov[1].iov_len = 0;
    }

    int len = _len;

    if(!epd->iov[epd->iov_buf_count].iov_base) {
        epd->iov[epd->iov_buf_count].iov_base = malloc(EP_D_BUF_SIZE);

        if(epd->iov[epd->iov_buf_count].iov_base == NULL) {
            return -2;
        }

        epd->iov[epd->iov_buf_count].iov_len = 0;
    }

    int buf_size = EP_D_BUF_SIZE;

    if(epd->iov_buf_count == 1) {
        buf_size = EP_D_BUF_SIZE - 8;//may be use 8 bytes at chunk mode
    }

    if(epd->iov[epd->iov_buf_count].iov_len + len > buf_size) {
        len = buf_size - epd->iov[epd->iov_buf_count].iov_len;

        if(len > _len) {
            len = _len;
        }
    }

    if(len > 0) {
        memcpy(epd->iov[epd->iov_buf_count].iov_base + epd->iov[epd->iov_buf_count].iov_len, data, len);
        epd->iov[epd->iov_buf_count].iov_len += len;
        epd->response_content_length += len;
        _len -= len;
    }

    if(_len > 0) {
        if(epd->iov_buf_count + 1 >= _MAX_IOV_COUNT) {
            return _len;
        }

        epd->iov_buf_count += 1;

        epd->iov[epd->iov_buf_count].iov_base = NULL;
        epd->iov[epd->iov_buf_count].iov_len = 0;

        return network_send(epd, data + len, _len);
    }

    return _len;
}

void free_epd_request(epdata_t *epd) /// for keepalive
{
    if(!epd) {
        return ;
    }

    if(epd->headers) {
        free(epd->headers);

        epd->headers = NULL;
    }

    int i = 0;

    for(i = 0; i < epd->iov_buf_count; i++) {
        free(epd->iov[i].iov_base);
        epd->iov[i].iov_base = NULL;
        epd->iov[i].iov_len = 0;
    }

    epd->response_header_length = 0;
    epd->response_content_length = 0;
    epd->iov_buf_count = 0;
    epd->response_buf_sended = 0;

    epd->contents = NULL;
    epd->data_len = 0;
    epd->header_len = 0;
    epd->_header_length = 0;
    epd->content_length = -1;
    epd->process_timeout = 0;

    if(!epd->websocket) {
        se_be_read(epd->se_ptr, network_be_read);

    } else {
        se_be_read(epd->se_ptr, websocket_be_read);
    }
}

void network_end_process(epdata_t *epd, int response_code)
{
    epd->status = STEP_WAIT;

    int i = 0;

    if(!response_code && epd->iov[0].iov_base) {
        char *hl = strtok(epd->iov[0].iov_base, "\n");

        if(hl) {
            hl = strtok(hl, " ");
            hl = strtok(NULL, " ");

            if(hl) {
                response_code = atoi(hl);
            }
        }
    }

    free(epd->iov[0].iov_base);
    epd->iov[0].iov_base = NULL;

    for(i = 1; i < epd->iov_buf_count; i++) {
        free(epd->iov[i].iov_base);
        epd->iov[i].iov_base = NULL;
        epd->iov[i].iov_len = 0;
    }

    epd->iov_buf_count = 0;

    if(epd->zs) {
        deflateEnd(epd->zs);
        free(epd->zs);
        epd->zs = NULL;
    }

    long ttime = longtime();

    if(!epd->uri && epd->headers) {
        epd->headers[epd->header_len ? epd->header_len : epd->data_len] = '\0';
        char *pt1 = NULL, *pt2 = NULL, *t1 = NULL, *t2 = NULL, *t3 = NULL;
        pt1 = epd->headers;
        i = 0;

        while(t1 = strtok_r(pt1, "\n", &pt1)) {
            if(++i == 1) { /// first line
                t2 = strtok_r(t1, " ", &t1);
                t3 = strtok_r(t1, " ", &t1);
                epd->http_ver = strtok_r(t1, " ", &t1);

                if(!epd->http_ver) {
                    break;
                }

                int len = strlen(epd->http_ver);

                if(epd->http_ver[len - 1] == 13) { // CR == 13
                    epd->http_ver[len - 1] = '\0';
                }

                if(t2 && t3) {
                    for(t1 = t2 ; *t1 ; *t1 = toupper(*t1), t1++);

                    epd->method = t2;
                    t1 = strtok_r(t3, "?", &t3);
                    t2 = strtok_r(t3, "?", &t3);
                    epd->uri = t1;

                    if(t2) {
                        epd->query = (t2 - 1);
                        epd->query[0] = '?';
                    }
                }

                continue;
            }

            t2 = strtok_r(t1, ":", &t1);

            if(t2) {
                for(t3 = t2; *t3; ++t3) {
                    *t3 = *t3 >= 'A' && *t3 <= 'Z' ? *t3 | 0x60 : *t3;
                }

                t3 = t2 + strlen(t2) + 1; //strtok_r ( t1, ":", &t1 )

                if(t3) {
                    int len = strlen(t3);

                    if(t3[len - 1] == 13) { /// 13 == CR
                        t3[len - 1] = '\0';
                        len -= 1;
                    }

                    if(len < 1) {
                        break;
                    }

                    if(t2[0] == 'h' && epd->host == NULL && strcmp(t2, "host") == 0) {
                        char *_t = strstr(t3, ":");

                        if(_t) {
                            _t[0] = '\0';
                        }

                        epd->host = t3 + (t3[0] == ' ' ? 1 : 0);

                    } else if(!epd->user_agent && t2[1] == 's' && strcmp(t2, "user-agent") == 0) {
                        epd->user_agent = t3 + (t3[0] == ' ' ? 1 : 0);

                    } else if(!epd->referer && t2[1] == 'e' && strcmp(t2, "referer") == 0) {
                        epd->referer = t3 + (t3[0] == ' ' ? 1 : 0);

                    } else if(!epd->if_modified_since && t2[1] == 'f' && strcmp(t2, "if-modified-since") == 0) {
                        epd->if_modified_since = t3 + (t3[0] == ' ' ? 1 : 0);
                    }
                }
            }
        }
    }

    if(ACCESS_LOG) log_writef(ACCESS_LOG,
                                  "%s - - [%s] %s \"%s %s %s\" %d %d %d %d %d \"%s\" \"%s\" %.3f\n",
                                  cached_ntoa(epd->client_addr),
                                  now_lc,
                                  epd->host ? epd->host : "-",
                                  epd->method ? epd->method : "-",
                                  epd->uri ? epd->uri : "/",
                                  epd->http_ver ? epd->http_ver : "-",
                                  epd->header_len,
                                  epd->content_length > 0 ? epd->content_length : 0,
                                  response_code,
                                  epd->response_header_length,
                                  epd->total_response_content_length - epd->response_header_length,
                                  epd->referer ? epd->referer : "-",
                                  epd->user_agent ? epd->user_agent : "-",
                                  (float)(ttime - epd->start_time) / 1000);

    if(epd->L && !epd->websocket) {
        reinit_lua_thread_env(epd->L);
    }

    if(epd->keepalive == 1 && !check_process_for_exit()) { // && epd->se_ptr
        update_timeout(epd->timeout_ptr, STEP_WAIT_TIMEOUT);
        free_epd_request(epd);

    } else {
        close_client(epd);
        return;
    }
}

static void network_add_chunk_metas(epdata_t *epd)
{
    if(!epd->http_ver || strlen(epd->http_ver) != 8 || epd->http_ver[7] != '1') {
        epd->keepalive = 0;
        return;
    }

    if(epd->iov[1].iov_base) {
        char _buf[20] = {0};
        _ultostr(&_buf, epd->response_content_length, 16);
        strcat(_buf, "\r\n");
        int _buf_len = strlen(_buf);

        memmove(epd->iov[1].iov_base + _buf_len, epd->iov[1].iov_base, epd->iov[1].iov_len);
        memcpy(epd->iov[1].iov_base, _buf, _buf_len);
        epd->iov[1].iov_len += _buf_len;

        int add_bytes = 2;

        if(epd->header_sended != 1) {
            add_bytes = 7;
        }

        if(epd->iov[epd->iov_buf_count].iov_len + add_bytes > EP_D_BUF_SIZE) {
            epd->iov_buf_count ++;
            epd->iov[epd->iov_buf_count].iov_base = malloc(EP_D_BUF_SIZE);
            epd->iov[epd->iov_buf_count].iov_len = 0;

        } else if(!epd->iov[epd->iov_buf_count].iov_base) {
            epd->iov[epd->iov_buf_count].iov_base = malloc(EP_D_BUF_SIZE);
            epd->iov[epd->iov_buf_count].iov_len = 0;
        }

        memcpy(epd->iov[epd->iov_buf_count].iov_base + epd->iov[epd->iov_buf_count].iov_len, "\r\n0\r\n\r\n", add_bytes);
        epd->iov[epd->iov_buf_count].iov_len += add_bytes;

        epd->response_content_length += add_bytes + _buf_len;

    } else {
        int add_bytes = 0;

        if(epd->header_sended != 1) {
            add_bytes = 5;
        }

        epd->iov[1].iov_base = malloc(EP_D_BUF_SIZE);
        memcpy(epd->iov[1].iov_base, "0\r\n\r\n", add_bytes);
        epd->iov[1].iov_len = add_bytes;
        epd->iov_buf_count = 2;

        epd->response_content_length += add_bytes;
    }
}

static char *network_build_header_out(epdata_t *epd, int is_flush, int *_len)
{
    if(epd->response_content_length > 1024 && epd->iov[1].iov_base
       && !is_binary(epd->iov[1].iov_base, epd->iov[1].iov_len)) {
        char *p = NULL;

        if(epd->headers) {
            p = (char *)stristr(epd->headers, "Accept-Encoding", epd->header_len);
        }

        if(p) {
            p += 16;
            int i = 0, m = strlen(p);

            for(; i < 20 && i < m; i++) {
                if(p[i] == '\n') {
                    break;
                }
            }

            p[i] = '\0';

            if(strstr(p, "deflate")) {
                epd->content_gzip_or_deflated = 2;

            } else if(strstr(p, "gzip")) {
                epd->content_gzip_or_deflated = 1;
            }

            p[i] = '\n';
        }
    }

    int len = 0;

    if(epd->iov[0].iov_base == NULL) {
        if(epd->content_gzip_or_deflated > 0) {
            epd->response_content_length = gzip_iov(epd, is_flush, (struct iovec *) &epd->iov, epd->iov_buf_count,
                                                    &epd->iov_buf_count);
        }

        len = sprintf(temp_buf,
                      "HTTP/1.1 200 OK\r\nServer: aLiLua/%s (%s)\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: %s\r\n%sContent-Length: %d\r\n\r\n",
                      ALILUA_VERSION, hostname, (epd->keepalive == 1 ? "keep-alive" : "close"),
                      (epd->content_gzip_or_deflated == 1 ? "Content-Encoding: gzip\r\n" : (epd->content_gzip_or_deflated == 2 ?
                              "Content-Encoding: deflate\r\n" : "")),
                      epd->response_content_length);
        epd->response_header_length = len;

    } else {
        char *p = (char *)stristr(epd->iov[0].iov_base, "content-length:", epd->response_header_length);

        if(p) {
            if(is_flush == 0) {
                p[0] = 'X';

            } else {
                epd->has_content_length_or_chunk_out = 1;
            }

        } else {
            if(stristr(epd->iov[0].iov_base, "transfer-encoding:", epd->response_header_length)) {
                epd->has_content_length_or_chunk_out = 2;
            }
        }

        if(epd->content_gzip_or_deflated > 0) {
            epd->response_content_length = gzip_iov(epd, is_flush, (struct iovec *) &epd->iov, epd->iov_buf_count,
                                                    &epd->iov_buf_count);
        }

        if(epd->has_content_length_or_chunk_out == 2) {
            network_add_chunk_metas(epd);
        }

        memcpy(temp_buf, epd->iov[0].iov_base, epd->response_header_length);

        if((p = (char *)stristr(temp_buf, "Connection:", epd->response_header_length))) {
            if(stristr(p, "close", epd->response_header_length - (p - temp_buf))) {
                epd->keepalive = 0;
            }

            if(epd->has_content_length_or_chunk_out < 1) {
                len = epd->response_header_length + sprintf(temp_buf + epd->response_header_length,
                        "Server: aLiLua/%s (%s)\r\nDate: %s\r\n%sContent-Length: %d\r\n\r\n", ALILUA_VERSION, hostname, now_gmt,
                        (epd->content_gzip_or_deflated == 1 ? "Content-Encoding: gzip\r\n" : (epd->content_gzip_or_deflated == 2 ?
                                "Content-Encoding: deflate\r\n" : "")),
                        epd->response_content_length);

            } else {
                len = epd->response_header_length + sprintf(temp_buf + epd->response_header_length,
                        "Server: aLiLua/%s (%s)\r\nDate: %s\r\n%s\r\n", ALILUA_VERSION, hostname, now_gmt,
                        (epd->content_gzip_or_deflated == 1 ? "Content-Encoding: gzip\r\n" : (epd->content_gzip_or_deflated == 2 ?
                                "Content-Encoding: deflate\r\n" : "")));
            }

        } else {
            if(epd->has_content_length_or_chunk_out < 1) {
                len = epd->response_header_length + sprintf(temp_buf + epd->response_header_length,
                        "Server: aLiLua/%s (%s)\r\nConnection: %s\r\nDate: %s\r\n%sContent-Length: %d\r\n\r\n", ALILUA_VERSION, hostname,
                        (epd->keepalive == 1 ? "keep-alive" : "close"), now_gmt,
                        (epd->content_gzip_or_deflated == 1 ? "Content-Encoding: gzip\r\n" : (epd->content_gzip_or_deflated == 2 ?
                                "Content-Encoding: deflate\r\n" : "")),
                        epd->response_content_length);

            } else {
                len = epd->response_header_length + sprintf(temp_buf + epd->response_header_length,
                        "Server: aLiLua/%s (%s)\r\nConnection: %s\r\nDate: %s\r\n%s\r\n", ALILUA_VERSION, hostname,
                        (epd->keepalive == 1 ? "keep-alive" : "close"), now_gmt,
                        (epd->content_gzip_or_deflated == 1 ? "Content-Encoding: gzip\r\n" : (epd->content_gzip_or_deflated == 2 ?
                                "Content-Encoding: deflate\r\n" : "")));
            }
        }

        epd->response_header_length = len;
    }

    *_len = len;
    return (char *)&temp_buf;
}

int network_be_read_on_clear(se_ptr_t *ptr);
void network_be_end(epdata_t *epd) // for lua function die
{
    if(epd->content_length > epd->data_len - epd->_header_length && epd->fd > -1) {
        if(epd->status != STEP_READ) {
            epd->status = STEP_READ;
            serv_status.reading_counts++;
        }

        se_be_read(epd->se_ptr, network_be_read_on_clear);
        return;
    }

    if(epd->process_timeout == 1 && epd->keepalive != -1) {
        epd->process_timeout = 0;
        free_epd(epd);
        return;
    }

    //printf ( "network_be_end %d\n" , ((se_ptr_t*)epd->se_ptr)->fd );
    update_timeout(epd->timeout_ptr, STEP_SEND_TIMEOUT);

    serv_status.success_counts++;
    epd->status = STEP_SEND;
    serv_status.sending_counts++;

    if(epd->header_sended == 0) {
        if(epd->iov[0].iov_base == NULL && epd->iov[1].iov_base == NULL && epd->response_sendfile_fd == -1) {
            epd->status = STEP_PROCESS;
            serv_status.sending_counts--;
            network_send_error(epd, 417, "");
            return;

        } else if(epd->response_sendfile_fd == -2) {
            epd->response_sendfile_fd = -1;
            epd->status = STEP_PROCESS;
            serv_status.sending_counts--;
            network_send_error(epd, 404, "File Not Found!");
            return;

        } else {
            int len = 0;
            char *out_headers = network_build_header_out(epd, 0, &len);

            if(len < EP_D_BUF_SIZE && epd->response_sendfile_fd <= -1 && epd->iov[1].iov_base && epd->iov[1].iov_len > 0) {
                if(epd->iov[0].iov_base == NULL) {
                    epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);

                    if(epd->iov[0].iov_base == NULL) {
                        epd->keepalive = 0;
                        epd->status = STEP_PROCESS;
                        serv_status.sending_counts--;
                        network_end_process(epd, epd->se_ptr ? 0 : 499);
                        return;
                    }
                }

                memcpy(epd->iov[0].iov_base, out_headers, len);
                epd->iov[0].iov_len = len;
                epd->response_content_length += len;

                epd->iov_buf_count += 1;

            } else {

                if(!epd->ssl) {
                    network_raw_send(epd->fd, out_headers, len);

                } else {
                    SSL_raw_send(epd->ssl, out_headers, len);
                }
            }

            if(epd->response_content_length == 0) {
                epd->response_content_length = len;

                if(!epd->iov[0].iov_base) {
                    epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);
                    memcpy(epd->iov[0].iov_base, out_headers, len);

                    if(epd->iov_buf_count < 1) {
                        epd->iov_buf_count = 1;
                    }
                }

                serv_status.sending_counts--;
                epd->status = STEP_PROCESS;
                network_end_process(epd, epd->se_ptr ? 0 : 499);
                return;

            }

            epd->response_buf_sended = 0;
        }

    } else {
        epd->header_sended = 2;

        if(epd->content_gzip_or_deflated > 0) {
            if(epd->iov[1].iov_base == NULL) {
                epd->iov[1].iov_base = malloc(EP_D_BUF_SIZE);
                epd->iov[1].iov_len = 0;
            }

            epd->response_content_length = gzip_iov(epd, 1, (struct iovec *) &epd->iov, epd->iov_buf_count,
                                                    &epd->iov_buf_count);
        }

        if(epd->has_content_length_or_chunk_out == 2) {
            network_add_chunk_metas(epd);
        }

        if(epd->iov[1].iov_base == NULL) {
            serv_status.sending_counts--;
            epd->status = STEP_PROCESS;
            network_end_process(epd, 0);
            return;

        } else {
            epd->iov_buf_count ++;
        }

    }

    if(!epd->se_ptr) {
        //client is gone
        serv_status.sending_counts--;
        epd->status = STEP_PROCESS;
        network_end_process(epd, 499);
        return;
    }

    se_be_write(epd->se_ptr, network_be_write);
}

int network_be_read_on_processing(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0, b = 0;

    while((n = recv(epd->fd, &temp_buf, 8192, 0)) > 0) {
        b = 1;
    }

    if(n <= 0 || b) {
        serv_status.active_counts--;

        se_delete(epd->se_ptr);
        epd->se_ptr = NULL;
        close(epd->fd);
        epd->fd = -1;
        epd->keepalive = 0;
    }

    return 1;
}

int network_be_read_on_clear(se_ptr_t *ptr)
{
    //printf("network_be_read_on_clear\n");
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0;

    while((n = recv(epd->fd, &temp_buf64k, 61440, 0)) >= 0) {
        if(n == 0) {
            epd->status = STEP_PROCESS;

            serv_status.active_counts--;

            se_delete(epd->se_ptr);
            epd->se_ptr = NULL;
            close(epd->fd);
            epd->fd = -1;
            epd->keepalive = 0;
            serv_status.reading_counts--;
            network_be_end(epd);
            return 0;
        }

        epd->data_len += n;

        if(epd->content_length <= epd->data_len - epd->_header_length) {
            epd->status = STEP_PROCESS;
            se_be_pri(epd->se_ptr, NULL);
            serv_status.reading_counts--;
            network_be_end(epd);
            return 0;
        }
    }

    if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        epd->status = STEP_PROCESS;

        serv_status.active_counts--;

        se_delete(epd->se_ptr);
        epd->se_ptr = NULL;
        close(epd->fd);
        epd->fd = -1;
        epd->keepalive = 0;
        serv_status.reading_counts--;
        network_be_end(epd);
    }
}

int send_100_continue_then_process(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;
    epd->next_proc = NULL;

    epd->iov[0].iov_len = 0;
    epd->response_content_length = 0;
    epd->iov_buf_count = 0;

    if(worker_process(epd, 0) != 0) {
        close_client(epd);
        epd = NULL;
    }
}

int network_be_read(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0;

    update_timeout(epd->timeout_ptr, STEP_READ_TIMEOUT);

    if(epd->headers == NULL) {
        epd->headers = malloc(BUFFER_SIZE);

        if(epd->headers == NULL) {
            LOGF(ERR, "malloc error");
            close_client(epd);
            return 0;
        }

        epd->buf_size = BUFFER_SIZE;
        epd->start_time = longtime();

    } else if(epd->data_len == epd->buf_size) {
        {
            char *_t = (char *) realloc(epd->headers, epd->buf_size + BUFFER_SIZE);

            if(_t != NULL) {
                epd->headers = _t;

            } else {
                LOGF(ERR, "malloc error");
                close_client(epd);
                return 0;
            }

            epd->buf_size += BUFFER_SIZE;
        }
    }

//    while((n = recv(epd->fd, epd->headers + epd->data_len, epd->buf_size - epd->data_len, 0)) >= 0) {
    while((n = (epd->ssl ? SSL_read(epd->ssl, epd->headers + epd->data_len, epd->buf_size - epd->data_len) :
                recv(epd->fd, epd->headers + epd->data_len, epd->buf_size - epd->data_len, 0))) >= 0) {
        if(n == 0) {
            if(epd->data_len > 0) {
                epd->status = STEP_PROCESS;
                serv_status.reading_counts--;
                epd->keepalive = 0;
                network_end_process(epd, 400);

            } else {
                close_client(epd);
            }

            epd = NULL;
            break;
        }

        if(epd->data_len + n >= epd->buf_size) {
            char *_t = (char *) realloc(epd->headers, epd->buf_size + BUFFER_SIZE);

            if(_t != NULL) {
                epd->headers = _t;

            } else {
                LOGF(ERR, "malloc error");
                close_client(epd);
                return 0;
            }

            epd->buf_size += BUFFER_SIZE;
        }

        if(epd->status != STEP_READ) {
            serv_status.reading_counts++;
            epd->status = STEP_READ;
            epd->data_len = n;

        } else {
            epd->data_len += n;
        }

        if(epd->_header_length < 1 && epd->data_len >= 4 && epd->headers) {
            int _get_content_length = 0;

            if(epd->headers[epd->data_len - 1] == '\n' &&
               (epd->headers[epd->data_len - 2] == '\n' ||
                (epd->headers[epd->data_len - 4] == '\r' &&
                 epd->headers[epd->data_len - 2] == '\r'))) {
                epd->_header_length = epd->data_len;

            } else {
                _get_content_length = 1;
                unsigned char *fp2 = (unsigned char *)stristr(epd->headers, "\r\n\r\n", epd->data_len);

                if(fp2) {
                    epd->_header_length = (fp2 - epd->headers) + 4;

                } else {
                    fp2 = (unsigned char *)stristr(epd->headers, "\n\n", epd->data_len);

                    if(fp2) {
                        epd->_header_length = (fp2 - epd->headers) + 2;
                    }
                }
            }

            /// check request header length
            if(max_request_header > 0 && (epd->_header_length > max_request_header || (epd->_header_length < 1
                                          && epd->data_len > max_request_header))) {
                network_send_error(epd, 413, "Request Header Too Large");
                return 0;
            }

            if(epd->_header_length > 0 && epd->content_length < 0) {
                /// not POST or PUT request
                if(_get_content_length == 0 && epd->headers[0] != 'P' && epd->headers[0] != 'p') {
                    epd->content_length = 0;

                } else {
                    int flag = 0;

                    unsigned char *fp = (unsigned char *)stristr(epd->headers, "\ncontent-length:", epd->data_len);

                    if(fp) {
                        int fp_at = fp - epd->headers + 16;
                        int i = 0, _oc;

                        for(i = fp_at; i < epd->data_len; i++) {
                            if(epd->headers[i] == '\r' || epd->headers[i] == '\n') {
                                flag = 1;
                                fp = epd->headers + fp_at;
                                _oc = epd->headers[i];
                                epd->headers[i] = '\0';
                                break;
                            }
                        }

                        if(flag) {
                            epd->content_length = atoi(fp);
                            epd->headers[i] = _oc;

                            /// check request body length
                            if(max_request_body > 0 && epd->content_length > max_request_body) {
                                network_send_error(epd, 413, "Request Entity Too Large");
                                return 0;
                            }

                        }
                    }
                }
            }

            if(epd->_header_length > 0
               && epd->_header_length < epd->data_len
               && epd->content_length < 1) {
                epd->iov[0].iov_base = NULL;
                epd->iov[0].iov_len = 0;
                epd->iov[1].iov_base = NULL;
                epd->iov[1].iov_len = 0;
                network_send_error(epd, 411, "");

                break;
            }
        }

        if(epd->_header_length > 0) {
            /*&& (epd->content_length < 1 || epd->content_length <= epd->data_len - epd->_header_length)*/ /*reading body on the fly*/
            /// start job
            epd->header_len = epd->_header_length;
            epd->headers[epd->data_len] = '\0';
            epd->header_len -= 1;
            epd->headers[epd->header_len] = '\0';

            if(epd->header_len < epd->data_len) {
                epd->contents = epd->headers + epd->header_len;

            }/* else {

                epd->content_length = 0;
            }*/

            if(USE_KEEPALIVE == 1 && (epd->keepalive == 1 || (stristr(epd->headers, "keep-alive", epd->header_len)))) {
                epd->keepalive = 1;
            }

            epd->response_header_length = 0;
            epd->iov_buf_count = 0;
            epd->response_content_length = 0;
            epd->total_response_content_length = 0;
            epd->response_sendfile_fd = -1;

            if(epd->content_length < 1 || epd->content_length <= epd->data_len - epd->_header_length) {
                // readed end
                se_be_read(epd->se_ptr, network_be_read_on_processing);

            } else {
                se_be_pri(epd->se_ptr, NULL); // be wait
            }

            if(epd->status == STEP_READ) {
                serv_status.reading_counts--;
                epd->status = STEP_PROCESS;

                serv_status.sec_process_counts[(now) % 5]++;
                serv_status.process_counts++;
                epd->method = NULL;
                epd->uri = NULL;
                epd->host = NULL;
                epd->query = NULL;
                epd->http_ver = NULL;
                epd->referer = NULL;
                epd->user_agent = NULL;
                epd->header_sended = 0;
                epd->has_content_length_or_chunk_out = 0;
                epd->content_gzip_or_deflated = 0;

                if(stristr(epd->headers, "100-continue", epd->_header_length)) {
                    //network_raw_send(epd->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
                    if(epd->iov[0].iov_base == NULL) {
                        epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);

                        if(epd->iov[0].iov_base == NULL) {
                            LOGF(ERR, "malloc error");
                            return 0;
                        }

                        memcpy(epd->iov[0].iov_base, "HTTP/1.1 100 Continue\r\n\r\n", 25);
                        epd->iov[0].iov_len = 25;
                        epd->response_content_length = 25;
                        epd->response_buf_sended = 0;
                        epd->iov_buf_count = 1;
                    }

                    epd->next_proc = send_100_continue_then_process;
                    epd->status = STEP_SEND;
                    serv_status.sending_counts++;
                    se_be_write(epd->se_ptr, network_be_write);

                } else {
                    /// output server status !!!!!!!!!!
                    {
                        int i, len;
                        char *uri = NULL;
                        uri = epd->headers;

                        for(i = 0; i < epd->header_len; i++)
                            if(uri[i] == ' ') {
                                break;
                            }

                        for(; i < epd->header_len; i++)
                            if(uri[i] != ' ') {
                                break;
                            }

                        uri = epd->headers + i;
                        len = strlen(uri);

                        for(i = 0; i < len; i++) {
                            if(uri[i] == '\r' || uri[i] == '\n' || uri[i] == ' ') {
                                break;
                            }
                        }

                        if(i > 11 && strncmp("/serv-status", uri, 12) == 0) {
                            epd->process_timeout = 0;

                            epd->iov[0].iov_base = NULL;
                            epd->iov[0].iov_len = 0;
                            epd->iov[1].iov_base = NULL;
                            epd->iov[1].iov_len = 0;

                            network_send_status(epd);
                            break;
                        }
                    }

                    /// end.
                    if(worker_process(epd, 0) != 0) {
                        close_client(epd);
                        epd = NULL;
                    }
                }
            }

            break;
        }
    }

    if(epd && n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        //LOGF(ERR, "error fd %d (%d) %s", epd->fd, errno, strerror(errno));
        close_client(epd);
        epd = NULL;
        return 0;
    }

    return 1;
}

static void *mempcpy(void *to, const void *from, size_t size)
{
    memcpy(to, from, size);
    return (char *)to + size;
}

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
static int SSL_writev(SSL *ssl, const struct iovec *vector, int count)
{

    char *buffer;
    register char *bp;
    size_t bytes = 0, to_copy;
    ssize_t bytes_written;
    int i;

    for(i = 0; i < count; ++i) {
        if(SSIZE_MAX - bytes < vector[i].iov_len) {
            return -1;
        }

        bytes += vector[i].iov_len;
    }

    if((buffer = (char *)malloc(bytes)) == NULL) {
        return -1;
    }

    to_copy = bytes;
    bp = buffer;

    for(i = 0; i < count; ++i) {
        size_t copy = MIN(vector[i].iov_len, to_copy);
        bp = mempcpy((void *)bp, (void *)vector[i].iov_base, copy);
        to_copy -= copy;

        if(to_copy == 0) {
            break;
        }
    }

    bytes_written = SSL_write(ssl, buffer, bytes);

    if(bytes_written < 0) {
        int status = SSL_get_error(ssl, bytes_written);
        errno = (status == SSL_ERROR_WANT_READ || status == SSL_ERROR_WANT_WRITE) ? EAGAIN : EPIPE;
    }

    free(buffer);
    return bytes_written;
}

int SSL_sendfile(SSL *ssl, int fd, size_t size)
{
    size = (size > 61440) ? 61440 : size;
    ssize_t rcnt = read(fd, temp_buf64k, size);

    if(rcnt <= 0) {
        return rcnt;
    }

    ssize_t tcnt = SSL_write(ssl, temp_buf64k, size);

    if(tcnt > 0 && tcnt < rcnt) {   /* If sent < read readjust file position */
        lseek(fd, tcnt - rcnt, SEEK_CUR);
    }

    return tcnt;
}

int network_be_write(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0;
    send_iov_count = 0;

    update_timeout(epd->timeout_ptr, STEP_SEND_TIMEOUT);
    //printf ( "network_be_write\n" );

    if(epd->status == STEP_SEND) {
        int j = 0, k = 0;
        n = 0;

        while(epd->response_buf_sended < epd->response_content_length && n >= 0) {
            if(epd->response_sendfile_fd == -1) {
                if(n > 0) {
                    epd->response_buf_sended += n;
                    n = 0;
                }

                if(epd->response_buf_sended >= epd->response_content_length) {
                    if(epd->header_sended == 5) {
                        epd->header_sended = 1;
                    }

                    //printf("%ld sended %ld\n", epd->response_content_length, epd->response_buf_sended);
                    serv_status.sending_counts--;
                    epd->status = STEP_PROCESS;
                    epd->total_response_content_length += epd->response_content_length;

                    if(epd->header_sended != 1) {
                        if(epd->next_proc) {
                            epd->next_proc(ptr);

                        } else {
                            network_end_process(epd, 0);
                        }

                    } else {
                        int i = 0;

                        for(i = 1; i < epd->iov_buf_count; i++) {
                            free(epd->iov[i].iov_base);
                            epd->iov[i].iov_base = NULL;
                            epd->iov[i].iov_len = 0;
                        }

                        epd->response_content_length = 0;
                        epd->iov_buf_count = 1;
                        epd->response_buf_sended = 0;
                        epd->status = STEP_PROCESS;

                        if(epd->next_proc) {
                            epd->next_proc(ptr);
                            return 0;
                        }

                        if(epd->content_length < 1 || epd->content_length <= epd->data_len - epd->_header_length) {
                            // readed end
                            se_be_read(epd->se_ptr, network_be_read_on_processing);

                        } else {
                            se_be_pri(epd->se_ptr, NULL); // be wait
                        }

                        lua_pushboolean(epd->L, 1);

                        lua_f_lua_uthread_resume_in_c(epd->L, 1);

                    }

                    break;
                }

                {
                    int all = 0;
                    send_iov_count = 0;
                    size_t sended = 0;
                    size_t be_len = 0;

                    for(j = ((epd->header_sended == 0 || epd->header_sended == 5) ? 0 : 1); j < epd->iov_buf_count; j++) {
                        sended += epd->iov[j].iov_len;

                        if(sended > epd->response_buf_sended) {
                            if(k == 0 && epd->response_buf_sended > 0) {
                                k = epd->response_buf_sended - (sended - epd->iov[j].iov_len);
                            }

                            send_iov[send_iov_count] = epd->iov[j];
                            send_iov_count++;
                            be_len += epd->iov[j].iov_len;
                        }

                    }

                    if(k > 0) {
                        send_iov[0].iov_base += k;
                        send_iov[0].iov_len -= k;
                        be_len -= k;
                        k = 0;
                    }

                    if(be_len < 1) {
                        LOGF(ERR, "%d writev error! %d %ld", epd->fd, send_iov_count, be_len);
                        //exit ( 1 );
                    }

                    //n = writev(epd->fd, send_iov, send_iov_count);
                    if(!epd->ssl) {
                        n = writev(epd->fd, send_iov, send_iov_count);

                    } else {
                        n = SSL_writev(epd->ssl, send_iov, send_iov_count);
                    }
                }

            } else {
                if(!epd->ssl) {
                    n = network_raw_sendfile(epd->fd, epd->response_sendfile_fd, &epd->response_buf_sended, epd->response_content_length);

                } else {
                    n = SSL_sendfile(epd->ssl, epd->response_sendfile_fd, epd->response_content_length);

                    if(n > 0) {
                        epd->response_buf_sended += n;
                    }
                }

                if(epd->response_buf_sended >= epd->response_content_length) {
                    epd->total_response_content_length += epd->response_content_length;
                    close(epd->response_sendfile_fd);
                    epd->response_sendfile_fd = -1;
                    /*
                    #ifdef linux
                    int set = 0;
                    setsockopt(epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof(int));
                    #endif
                    */
                    serv_status.sending_counts--;
                    epd->status = STEP_PROCESS;

                    if(epd->next_proc) {
                        epd->next_proc(ptr);

                    } else {
                        network_end_process(epd, 0);
                    }

                    break;
                }
            }

            if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                epd->keepalive = 0;

                if(epd->response_sendfile_fd > -1) {
                    /*
                    #ifdef linux
                    int set = 0;
                    setsockopt(epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof(int));
                    #endif
                    */
                    close(epd->response_sendfile_fd);
                }

                serv_status.sending_counts--;
                epd->status = STEP_PROCESS;
                epd->total_response_content_length += epd->response_content_length;

                if(epd->header_sended != 1) {
                    if(epd->next_proc) {
                        epd->next_proc(ptr);

                    } else {
                        network_end_process(epd, 499);
                    }

                } else {
                    int i = 0;

                    for(i = 1; i < epd->iov_buf_count; i++) {
                        free(epd->iov[i].iov_base);
                        epd->iov[i].iov_base = NULL;
                        epd->iov[i].iov_len = 0;
                    }

                    epd->response_content_length = 0;
                    epd->iov_buf_count = 1;
                    epd->response_buf_sended = 0;
                    epd->status = STEP_PROCESS;

                    if(epd->next_proc) {
                        epd->next_proc(ptr);
                        return 0;
                    }

                    lua_pushboolean(epd->L, 0);
                    lua_pushstring(epd->L, "client closed!");

                    if(epd->content_length < 1 || epd->content_length <= epd->data_len - epd->_header_length) {
                        // readed end
                        se_be_read(epd->se_ptr, network_be_read_on_processing);

                    } else {
                        se_be_pri(epd->se_ptr, NULL); // be wait
                    }

                    lua_f_lua_uthread_resume_in_c(epd->L, 2);

                }

                break;
            }
        }
    }

    return 0;
}

int network_flush(epdata_t *epd)
{
    if(epd->header_sended == 0) {
        if(epd->status == STEP_READ) {
            serv_status.reading_counts--;
        }

        epd->status = STEP_SEND;
        serv_status.sending_counts++;
        char *p = (char *)stristr(epd->iov[0].iov_base, "content-length:", epd->response_header_length);

        if(p) {
            epd->has_content_length_or_chunk_out = 1;

        } else if(!stristr(epd->iov[0].iov_base, "transfer-encoding:", epd->response_header_length)) {
            if(epd->http_ver && strlen(epd->http_ver) == 8 && epd->http_ver[7] == '1') {
                network_send_header(epd, "Transfer-Encoding: chunked");
            }

            epd->has_content_length_or_chunk_out = 2;
        }

        epd->header_sended = 1;
        int len = 0;
        char *out_headers = network_build_header_out(epd, 0, &len);
        epd->header_sended = 5;

        if(len < EP_D_BUF_SIZE && epd->response_sendfile_fd <= -1 && epd->iov[1].iov_base && epd->iov[1].iov_len > 0) {
            if(epd->iov[0].iov_base == NULL) {
                epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);

                if(epd->iov[0].iov_base == NULL) {
                    epd->keepalive = 0;
                    LOGF(ERR, "malloc error!");
                    epd->status = STEP_PROCESS;
                    serv_status.sending_counts--;
                    return 0;
                }
            }

            memcpy(epd->iov[0].iov_base, out_headers, len);
            epd->iov[0].iov_len = len;
            epd->response_content_length += len;

            epd->iov_buf_count += 1;

        } else {
            if(!epd->ssl) {
                network_raw_send(epd->fd, out_headers, len);

            } else {
                SSL_raw_send(epd->ssl, out_headers, len);
            }
        }

        if(epd->response_content_length == 0) {
            epd->response_content_length = len;

            if(!epd->iov[0].iov_base) {
                epd->iov[0].iov_base = malloc(EP_D_BUF_SIZE);
                memcpy(epd->iov[0].iov_base, out_headers, len);

                if(epd->iov_buf_count < 1) {
                    epd->iov_buf_count = 1;
                }
            }

        }

        epd->response_buf_sended = 0;

        update_timeout(epd->timeout_ptr, STEP_SEND_TIMEOUT);

        se_be_write(epd->se_ptr, network_be_write);

        return 1;

    } else {
        if(epd->iov[1].iov_base == NULL || epd->iov[1].iov_len < 1) {
            return 0;
        }

        if(epd->status == STEP_READ) {
            serv_status.reading_counts--;
        }

        epd->status = STEP_SEND;
        serv_status.sending_counts++;

        if(epd->content_gzip_or_deflated > 0 && epd->iov[1].iov_base != NULL) {
            epd->response_content_length = gzip_iov(epd, 1, (struct iovec *) &epd->iov, epd->iov_buf_count,
                                                    &epd->iov_buf_count);
        }

        if(epd->has_content_length_or_chunk_out == 2) {
            network_add_chunk_metas(epd);
        }

        epd->iov_buf_count ++;

        update_timeout(epd->timeout_ptr, STEP_SEND_TIMEOUT);

        se_be_write(epd->se_ptr, network_be_write);

        return 1;

    }

    return 0;
}
