#include "main.h"
#include "config.h"
#include "network.h"

int worker_n = 0;
static char buf_4096[4096] = {0};
static int working_at_fd = 0;
extern lua_State *_L;
extern int main_handle_ref;

extern logf_t *ACCESS_LOG;

static void on_exit_handler()
{
    sync_logs(ACCESS_LOG);
    LOGF(ALERT, "worker %d exited", worker_n);
}

static int dump_smp_link_time = 0;
static int other_simple_jobs()
{
    check_lua_sleep_timeouts();
    sync_serv_status();

    if(now - dump_smp_link_time > 60){
        dump_smp_link_time = now;
        dump_smp_link();
    }

    return 1; // return 0 will be exit the worker
}

void free_epd(epdata_t *epd)
{
    if(!epd) {
        return;
    }

    if(epd->headers) {
        if(epd->headers != (unsigned char*)&epd->iov) {
            free(epd->headers);
        }
    }

    int i = 0;

    for(i = 0; i < epd->iov_buf_count && i < _MAX_IOV_COUNT; i++) {
        free(epd->iov[i].iov_base);
        epd->iov[i].iov_base = NULL;
    }

    free(epd);
}

void close_client(epdata_t *epd)
{
    if(!epd) {
        return;
    }

    if(epd->status == STEP_READ) {
        serv_status.reading_counts--;

    } else if(epd->status == STEP_SEND) {
        serv_status.sending_counts--;
    }

    se_delete(epd->se_ptr);
    delete_timeout(epd->timeout_ptr);
    epd->timeout_ptr = NULL;

    if(epd->fd > -1) {
        serv_status.active_counts--;
        close(epd->fd);
        epd->fd = -1;
    }

    if(epd->websocket) {
        if(epd->websocket->websocket_handles > 0) {
            luaL_unref(epd->websocket->ML, LUA_REGISTRYINDEX, epd->websocket->websocket_handles);
        }

        lua_resume(epd->websocket->ML, 0);
        free(epd->websocket);
        epd->websocket = NULL;
    }

    free_epd(epd);
}

static void timeout_handle(void *ptr)
{
    epdata_t *epd = ptr;

    if(epd->status == STEP_READ) {
        serv_status.reading_counts--;

    } else if(epd->status == STEP_SEND) {
        serv_status.sending_counts--;
    }

    epd->status = STEP_WAIT;

    close_client(epd);
}

int worker_process(epdata_t *epd, int thread_at)
{
    //printf("worker_process\n");
    working_at_fd = epd->fd;
    //network_send_error(epd, 503, "Lua Error: main function not found !!!");return 0;
    //network_send(epd, "aaa", 3);network_be_end(epd);return 0;
    add_io_counts();
    lua_State *L = (_L);   //lua_newthread

    if(main_handle_ref != 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, main_handle_ref);

    } else {
        lua_getglobal(L, "main");
    }

    int init_tables = 0;
    char *pt1 = NULL, *pt2 = NULL, *t1 = NULL, *t2 = NULL, *t3 = NULL, *query = NULL;

    int is_form_post = 0;
    char *boundary_post = NULL;
    char *cookies = NULL;
    pt1 = epd->headers;
    int i = 0;

    epd->uri = NULL;
    epd->host = NULL;
    epd->query = NULL;
    epd->http_ver = NULL;
    epd->referer = NULL;
    epd->user_agent = NULL;
    epd->if_modified_since = NULL;

    while(t1 = strtok_r(pt1, "\n", &pt1)) {
        if(++i == 1) {    /// first line
            t2 = strtok_r(t1, " ", &t1);
            t3 = strtok_r(t1, " ", &t1);
            epd->http_ver = strtok_r(t1, " ", &t1);

            if(!epd->http_ver) {
                return 1;

            } else {
                if(init_tables == 0) {
                    lua_pushlightuserdata(L, epd);
                    lua_newtable(L);
                }
            }

            int len = strlen(epd->http_ver);

            if(epd->http_ver[len - 1] == 13) {    // CR == 13
                epd->http_ver[len - 1] = '\0';
            }

            if(t2 && t3) {
                for(t1 = t2 ; *t1 ; *t1 = toupper(*t1), t1++);

                epd->method = t2;
                lua_pushstring(L, t2);
                lua_setfield(L, -2, "method");
                t1 = strtok_r(t3, "?", &t3);
                t2 = strtok_r(t3, "?", &t3);
                epd->uri = t1;
                lua_pushstring(L, t1);
                lua_setfield(L, -2, "uri");

                if(t2) {
                    epd->query = t2;
                    query = t2;
                    lua_pushstring(L, t2);
                    lua_setfield(L, -2, "query");
                }
            }

            continue;
        }

        t2 = strtok_r(t1, ":", &t1);

        if(t2) {
            for(t3 = t2; *t3; ++t3) {
                *t3 = *t3 >= 'A' && *t3 <= 'Z' ? *t3 | 0x60 : *t3;
            }

            t3 = t2 + strlen(t2) + 1;    //strtok_r ( t1, ":", &t1 )

            if(t3) {
                int len = strlen(t3);

                if(t3[len - 1] == 13) {    /// 13 == CR
                    t3[len - 1] = '\0';
                }

                lua_pushstring(L, t3 + (t3[0] == ' ' ? 1 : 0));
                lua_setfield(L, -2, t2);

                /// check content-type
                if(t2[1] == 'o' && strcmp(t2, "content-type") == 0) {
                    if(stristr(t3, "x-www-form-urlencoded", len)) {
                        is_form_post = 1;

                    } else if(stristr(t3, "multipart/form-data", len)) {
                        boundary_post = stristr(t3, "boundary=", len - 2);
                    }

                } else if(!cookies && t2[1] == 'o' && strcmp(t2, "cookie") == 0) {
                    cookies = t3 + (t3[0] == ' ' ? 1 : 0);

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

    char *client_ip = inet_ntoa(epd->client_addr);
    lua_pushstring(L, client_ip);
    lua_setfield(L, -2, "remote-addr");

    lua_newtable(L);    /// _GET

    if(query) {    /// parse query string /?a=1&b=2
        while(t1 = strtok_r(query, "&", &query)) {
            t2 = strtok_r(t1, "=", &t1);
            t3 = strtok_r(t1, "=", &t1);

            if(t2 && t3 && strlen(t2) > 0 && strlen(t3) > 0) {
                size_t len, dlen;
                u_char *p;
                u_char *src, *dst;
                len = strlen(t3);
                p = malloc(len);
                p[0] = '\0';
                dst = p;
                urldecode(&dst, (u_char**)&t3, len, 0);
                lua_pushlstring(L, (char *) p, dst - p);

                len = strlen(t2);

                if(len > 4096) {
                    free(p);
                    p = malloc(len);
                }

                p[0] = '\0';
                dst = p;

                urldecode(&dst, (u_char**)&t2, len, 0);
                p[dst - p] = '\0';
                lua_setfield(L, -2, p);
                free(p);
            }
        }
    }

    lua_newtable(L);    /// _COOKIE

    if(cookies) {
        while(t1 = strtok_r(cookies, ";", &cookies)) {
            t2 = strtok_r(t1, "=", &t1);
            t3 = strtok_r(t1, "=", &t1);

            if(t2 && t3 && strlen(t2) > 0 && strlen(t3) > 0) {
                size_t len, dlen;
                u_char *p;
                u_char *src, *dst;
                len = strlen(t3);
                p = malloc(len);
                p[0] = '\0';
                dst = p;
                urldecode(&dst, (u_char**)&t3, len, 0);
                lua_pushlstring(L, (char *) p, dst - p);

                len = strlen(t2);

                if(len > 4096) {
                    free(p);
                    p = malloc(len);
                }

                p[0] = '\0';
                dst = p;

                urldecode(&dst, (u_char**)&t2, len, 0);
                p[dst - p] = '\0';
                lua_setfield(L, -2, p + (p[0] == ' ' ? 1 : 0));
                free(p);
            }
        }
    }

    lua_newtable(L);    /// _POST

    if(is_form_post == 1
       && epd->contents) {  /// parse post conents text=aa+bb&text2=%E4%B8%AD%E6%96%87+aa
        pt1 = epd->contents;

        while(t1 = strtok_r(pt1, "&", &pt1)) {
            t2 = strtok_r(t1, "=", &t1);
            t3 = strtok_r(t1, "=", &t1);

            if(t2 && t3 && strlen(t2) > 0 && strlen(t3) > 0) {
                size_t len, dlen;
                u_char *p;
                u_char *src, *dst;
                len = strlen(t3);
                p = malloc(len);
                p[0] = '\0';
                dst = p;
                urldecode(&dst, (u_char**)&t3, len, 0);
                lua_pushlstring(L, (char *) p, dst - p);
                free(p);
                //lua_pushstring(L, t3);
                lua_setfield(L, -2, t2);
            }
        }

    } else if(boundary_post) {    /// parse boundary body
        boundary_post += 9;
        int blen = strlen(boundary_post);
        int len = 0;
        char *start = epd->contents, *p2 = NULL, *p1 = NULL, *pp = NULL, *value = NULL;
        int i = 0;

        do {
            p2 = strstr(start, boundary_post);

            if(p2) {
                start = p2 + blen;
            }

            if(p1) {
                p1 += blen;

                if(p2) {
                    * (p2 - 4) = '\0';

                } else {
                    break;
                }

                len = p2 - p1;
                value = stristr(p1, "\r\n\r\n", len);

                if(value && value[4] != '\0') {
                    value[0] = '\0';
                    value += 4;
                    char *keyname = strstr(p1, "name=\"");
                    char *filename = NULL;
                    char *content_type = NULL;

                    if(keyname) {
                        keyname += 6;

                        for(pp = keyname; *pp != '\0'; pp++) {
                            if(*pp == '"') {
                                *pp = '\0';
                                p1 = pp + 2;
                                break;
                            }
                        }

                        filename = strstr(p1, "filename=\"");

                        if(filename) {    /// is file filed
                            filename += 10;

                            for(pp = filename; *pp != '\0'; pp++) {
                                if(*pp == '"') {
                                    *pp = '\0';
                                    p1 = pp + 2;
                                    break;
                                }
                            }

                            content_type = strstr(p1, "Content-Type:");

                            if(content_type) {
                                content_type += 13;

                                if(content_type[0] == ' ') {
                                    content_type += 1;
                                }
                            }

                            lua_newtable(L);
                            lua_pushstring(L, filename);
                            lua_setfield(L, -2, "filename");
                            lua_pushstring(L, content_type);
                            lua_setfield(L, -2, "type");
                            lua_pushnumber(L, p2 - value - 4);
                            lua_setfield(L, -2, "size");
                            lua_pushlstring(L, value, p2 - value - 4);
                            lua_setfield(L, -2, "data");

                            lua_setfield(L, -2, keyname);

                        } else {
                            lua_pushstring(L, value);
                            lua_setfield(L, -2, keyname);
                        }
                    }
                }
            }

            p1 = p2 + 2;
        } while(p2);
    }

    epd->contents = NULL;
    epd->content_length = 0;
    epd->iov[0].iov_base = NULL;
    epd->iov[0].iov_len = 0;
    epd->iov[1].iov_base = NULL;
    epd->iov[1].iov_len = 0;

    if(lua_pcall(L, 5, 0, 0)) {
        if(lua_isstring(L, -1)) {
            LOGF(ERR, "Lua:error %s\n", lua_tostring(L, -1));
            network_send_error(epd, 503, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    return 0;

    //network_send_error(epd, 503, "Lua Error: main function not found !!!");
}

static void be_accept(int client_fd, struct in_addr client_addr)
{
    if(!set_nonblocking(client_fd, 1)) {
        close(client_fd);
        return;
    }

    epdata_t *epd = malloc(sizeof(epdata_t));

    if(!epd) {
        close(client_fd);
        return;
    }

    epd->fd = client_fd;
    epd->client_addr = client_addr;
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
    epd->websocket = NULL;

    epd->se_ptr = se_add(loop_fd, client_fd, epd);
    epd->timeout_ptr = add_timeout(epd, STEP_WAIT_TIMEOUT, timeout_handle);

    se_be_read(epd->se_ptr, network_be_read);

    serv_status.active_counts++;
    serv_status.connect_counts++;
}

void worker_main(int _worker_n)
{
    worker_n = _worker_n;
    attach_on_exit(on_exit_handler);

    if(is_daemon == 1) {
        set_process_user(/*user*/ NULL, /*group*/ NULL);
    }

    init_mime_types();
    shm_serv_status = _shm_serv_status->p;
    memcpy(shm_serv_status, &serv_status, sizeof(serv_status_t));

    /// 进入 loop 处理循环
    loop_fd = se_create(4096);
    se_accept(loop_fd, server_fd, be_accept);

    se_loop(loop_fd, 10, other_simple_jobs); // loop

    exit(0);
}
