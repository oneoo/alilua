#include "main.h"
#include "config.h"
#include "network.h"
#include "vhost.h"
#include "../coevent/src/coevent.h"
#include "cached-ntoa.h"
#include "cached-access.h"

int worker_n = 0;
static char buf_4096[4096] = {0};
static int working_at_fd = 0;
extern lua_State *_L;
extern int lua_routed;

extern SSL_CTX *ssl_ctx;
extern int ssl_epd_idx;

extern logf_t *ACCESS_LOG;

extern const char *lua_path;
extern char process_chdir[924];

static int exited = 0;
static void on_exit_handler()
{
    if(exited) {
        return;
    }

    now += 10;
    serv_status.waiting_counts = 0;
    serv_status.reading_counts = 0;
    serv_status.sending_counts = 0;
    serv_status.active_counts = 0;
    sync_serv_status();

    exited = 1;
    sync_logs(ACCESS_LOG);
    LOGF(ALERT, "worker %d exited", worker_n);

    if(getarg("gcore") && !check_process_for_exit()) {
        char cmd[50] = {0};
        sprintf(cmd, "gcore -o dump %u", getpid());
        system(cmd);
    }
}

static int dump_smp_link_time = 0;
static int flush_logs_timer = 0;
static int other_simple_jobs()
{
    coevnet_module_do_other_jobs();
    sync_serv_status();
#ifdef SMPDEBUG

    if(now - dump_smp_link_time > 60) {
        dump_smp_link_time = now;
        dump_smp_link();
    }

#endif

    if(++flush_logs_timer > 100) {
        flush_logs_timer = 0;
        sync_logs(LOGF_T);
        sync_logs(ACCESS_LOG);
    }

    return 1; // return 0 will be exit the worker
}

void free_epd(epdata_t *epd)
{
    if(!epd) {
        return;
    }

    if(epd->headers) {
        if(epd->headers != (unsigned char *)&epd->iov) {
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

    if(epd->ssl && epd->fd > -1) {
        if(!SSL_shutdown(epd->ssl)) {
            shutdown(epd->fd, 1);
            SSL_shutdown(epd->ssl);
        }

        SSL_free(epd->ssl);
        epd->ssl = NULL;
    }

    if(epd->L) {
        if(epd->status == STEP_PROCESS) {
            LOGF(ERR, "at working!!!");
        }

        release_lua_thread(epd->L);
        epd->L = NULL;
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

    free_epd(epd);
}

static void timeout_handle(void *ptr)
{
    epdata_t *epd = ptr;

    if(epd->status == STEP_READ) {
        serv_status.reading_counts--;
        epd->keepalive = 0;
        //LOGF(ERR, "Read Timeout!");
        epd->status = STEP_PROCESS;
        network_send_error(epd, 400, "Timeout!");
        return;

    } else if(epd->status == STEP_SEND) {
        //LOGF(ERR, "Send Timeout!");
        serv_status.sending_counts--;
    }

    if(epd->status == STEP_PROCESS && epd->L) {
        LOGF(ERR, "Process Timeout(continue)");
        update_timeout(epd->timeout_ptr, STEP_PROCESS_TIMEOUT);
        return;
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

    if(epd->ssl && !epd->ssl_verify) {
        network_send_error(epd, 400, "No required SSL certificate was send");
        return 0;
    }

    lua_State *L = epd->L;

    if(!L) {
        epd->L = new_lua_thread(_L);

        if(!epd->L) {
            network_send_error(epd, 503, "Lua Error: Thread pool full !!!");
            LOGF(ERR, "Lua Error: Thread pool full !!!");
            return 0;
        }

        lua_pushlightuserdata(epd->L, epd);
        lua_setglobal(epd->L, "__epd__");

        L = epd->L;
    }

    lua_getglobal(L, "process");

    update_timeout(epd->timeout_ptr, STEP_PROCESS_TIMEOUT + 100);

    int init_tables = 0;
    char *pt1 = NULL, *pt2 = NULL, *t1 = NULL, *t2 = NULL, *t3 = NULL;

    int is_form_post = 0;
    char *cookies = NULL;
    pt1 = epd->headers;

    if(pt1[0] == '\0') {
        pt1 = pt1 + 1;
    }

    int i = 0;

    epd->uri = NULL;
    epd->host = NULL;
    epd->query = NULL;
    epd->http_ver = NULL;
    epd->referer = NULL;
    epd->user_agent = NULL;
    epd->if_modified_since = NULL;

    //epd->start_time = longtime();

    while(t1 = strtok_r(pt1, "\n", &pt1)) {
        if(++i == 1) { /// first line
            t2 = strtok_r(t1, " ", &t1);
            t3 = strtok_r(t1, " ", &t1);
            epd->http_ver = strtok_r(t1, " ", &t1);

            if(!epd->http_ver) {
                network_send_error(epd, 400, "Bad Request");
                return 0;

            } else {
                if(init_tables == 0) {
                    lua_createtable(L, 0, 20); //headers
                    init_tables = 1;
                }
            }

            int len = strlen(epd->http_ver);

            if(epd->http_ver[len - 1] == 13) { // CR == 13
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
                    epd->query = (t2 - 1);
                    epd->query[0] = '?';
                    lua_pushstring(L, epd->query);
                    lua_setfield(L, -2, "query");
                }
            }

            continue;
        }

        t2 = strtok_r(t1, ":", &t1);

        if(t2) {
            if(t2[0] == '\r') {
                break;
            }

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

                lua_pushstring(L, t3 + (t3[0] == ' ' ? 1 : 0));
                lua_setfield(L, -2, t2);

                /// check content-type
                if(t2[0] == 'h' && epd->host == NULL && strcmp(t2, "host") == 0) {
                    char *_t = strstr(t3, ":");

                    if(_t) {
                        _t[0] = '\0';
                    }

                    epd->host = t3 + (t3[0] == ' ' ? 1 : 0);

                } else if(t2[1] == 'o' && strcmp(t2, "content-type") == 0) {
                    if(stristr(t3, "x-www-form-urlencoded", len)) {
                        is_form_post = 1;

                    } else if(stristr(t3, "multipart/form-data", len)) {
                        epd->boundary = (char *)stristr(t3, "boundary=", len - 2);

                        if(epd->boundary) {
                            epd->boundary += 9;
                        }
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

    if(init_tables == 0) {
        network_send_error(epd, 400, "Bad Request");
        return 0;
    }

    const char *client_ip = cached_ntoa(epd->client_addr);
    lua_pushstring(L, client_ip);
    lua_setfield(L, -2, "remote-addr");
    lua_pushstring(L, cached_ntoa(epd->server_addr));
    lua_setfield(L, -2, "server-addr");

    lua_setglobal(L, "headers");

    lua_createtable(L, 0, 20); /// _GET

    if(epd->query) { /// parse query string /?a=1&b=2
        char *last = NULL;
        int plen = 0;
        int qlen = strlen(epd->query) - 1;

        t1 = (char *)strsplit(epd->query + 1, qlen, "&", &last, &plen);

        while(t1) {
            char *last2 = NULL;
            int plen2 = 0;
            int plen3 = 0;

            t2 = (char *)strsplit(t1, plen, "=", &last2, &plen2);
            t3 = (char *)strsplit(t1, plen, "=", &last2, &plen3);

            if(t2 && plen2 > 0 && plen3 > 0 && plen2 <= 4096 && plen3 <= 4096) {
                size_t dlen = 0;
                u_char *p = 0;
                u_char *src = NULL, *dst = NULL;

                p = (u_char *)&buf_4096;
                p[0] = '\0';
                dst = p;
                dlen = urldecode(&p, (u_char **)&t3, plen3, RAW_UNESCAPE_URL);
                lua_pushlstring(L, (char *) p, dlen);

                p[0] = '\0';
                dst = p;

                dlen = urldecode(&dst, (u_char **)&t2, plen2, RAW_UNESCAPE_URL);
                p[dlen] = '\0';
                lua_setfield(L, -2, p);
            }

            t1 = (char *)strsplit(epd->query + 1, qlen, "&", &last, &plen);
        }
    }

    lua_setglobal(L, "_GET");

    lua_createtable(L, 0, 20); /// _COOKIE

    if(cookies) {
        while(t1 = strtok_r(cookies, ";", &cookies)) {
            t2 = strtok_r(t1, "=", &t1);
            t3 = strtok_r(t1, "=", &t1);

            if(t2 && t3) {
                size_t t2_len = strlen(t2), t3_len = strlen(t3);

                if(t2_len > 0 && t3_len > 0) {
                    size_t dlen = 0, mlen = (t3_len > t2_len ? t3_len : t2_len);
                    u_char *p = (u_char *)&buf_4096;
                    u_char *src = NULL, *dst = NULL;

                    if(mlen > 4096) {
                        p = malloc(mlen);
                    }

                    p[0] = '\0';
                    dst = p;
                    dlen = urldecode(&dst, (u_char **)&t3, t3_len, RAW_UNESCAPE_URL);
                    lua_pushlstring(L, (char *) p, dlen);

                    p[0] = '\0';
                    dst = p;

                    dlen = urldecode(&dst, (u_char **)&t2, t2_len, RAW_UNESCAPE_URL);
                    p[dlen] = '\0';
                    lua_setfield(L, -2, p + (p[0] == ' ' ? 1 : 0));

                    if(mlen > 4096) {
                        free(p);
                    }
                }
            }
        }
    }

    lua_setglobal(L, "_COOKIE");

    epd->vhost_root = get_vhost_root(epd->host, &epd->vhost_root_len);

    memcpy(buf_4096, epd->vhost_root, epd->vhost_root_len + 1);
    sprintf(buf_4096 + epd->vhost_root_len + 1, "?.lua;%s/lua-libs/?.lua;%s", process_chdir, lua_path);

    lua_pushstring(L, buf_4096);
    lua_getglobal(L, "package");
    lua_insert(L, -2); //-1 bufres -2 package
    lua_setfield(L, -2, "path"); //-1: path -2: package
    lua_pop(L, 1); //void

    lua_pushlstring(L, epd->vhost_root, epd->vhost_root_len); /// host root

    lua_setglobal(L, "__root");

    lua_pushstring(L, epd->vhost_root + epd->vhost_root_len); /// index-route.lua file

    epd->iov[0].iov_base = NULL;
    epd->iov[0].iov_len = 0;
    epd->iov[1].iov_base = NULL;
    epd->iov[1].iov_len = 0;

    lua_routed = 0;

    if(lua_f_lua_uthread_resume_in_c(L, 1) == LUA_ERRRUN) {
        if(lua_isstring(L, -1)) {
            LOGF(ERR, "Lua:error %s", lua_tostring(L, -1));
            network_send_error(epd, 503, lua_tostring(L, -1));

        } else {
            network_send_error(epd, 503, "UNKNOW!");
        }

        lua_pop(L, 1);
    }

    return 0;
}

static void be_accept(int client_fd, struct in_addr client_addr)
{
    int sockaddr_len = sizeof(struct sockaddr);
    struct sockaddr_in addr;
    /* Disable the Nagle (TCP No Delay) algorithm */
    int flag = 1;
    int ret = setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    if(ret == -1) {
        LOGF(ERR, "Couldn't setsockopt(TCP_NODELAY)");
    }

    if(!set_nonblocking(client_fd, 1)) {
        close(client_fd);
        return;
    }

    epdata_t *epd = malloc(sizeof(epdata_t));

    if(!epd) {
        close(client_fd);
        return;
    }

    bzero(epd, sizeof(epdata_t));

    epd->L = new_lua_thread(_L);

    if(epd->L) {
        lua_pushlightuserdata(epd->L, epd);
        lua_setglobal(epd->L, "__epd__");
    }

    epd->fd = client_fd;
    epd->client_addr = client_addr;
    getsockname(epd->fd, (struct sockaddr *) &addr, &sockaddr_len);
    epd->server_addr = addr.sin_addr;
    epd->status = STEP_WAIT;
    epd->content_length = -1;
    epd->keepalive = -1;
    epd->response_sendfile_fd = -1;
    //epd->start_time = longtime();

    epd->se_ptr = se_add(loop_fd, client_fd, epd);
    epd->timeout_ptr = add_timeout(epd, STEP_WAIT_TIMEOUT, timeout_handle);

    se_be_read(epd->se_ptr, network_be_read);

    serv_status.active_counts++;
    serv_status.connect_counts++;
}

int _be_ssl_accept(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    if(!epd->ssl) {
        epd->ssl = SSL_new(ssl_ctx);

        if(epd->ssl == NULL) {
            LOGF(ERR, "SSL_new");
            close_client(epd);
            return 0;
        }

        if(SSL_set_fd(epd->ssl, epd->fd) != 1) {
            SSL_free(epd->ssl);
            epd->ssl = NULL;
            LOGF(ERR, "SSL_set_fd");
            close_client(epd);
            return 0;
        }

        if(ssl_epd_idx > -1) {
            if(SSL_set_ex_data(epd->ssl, ssl_epd_idx, epd) != 1) {
                SSL_free(epd->ssl);
                LOGF(ERR, "SSL_set_ex_data");
                int fd = epd->fd;
                free(epd);
                close(fd);
                return 0;
            }
        }
    }

    int ret = SSL_accept(epd->ssl);

    if(ret != 1) {
        int ssl_err = SSL_get_error(epd->ssl, ret);

        if(ssl_err != SSL_ERROR_WANT_READ && ssl_err != SSL_ERROR_WANT_WRITE) {
            close_client(epd);
            return 0;
        }

        return 0;
    }

    se_be_read(epd->se_ptr, network_be_read);
}

static void be_ssl_accept(int client_fd, struct in_addr client_addr)
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

    bzero(epd, sizeof(epdata_t));

    if(NULL == ssl_ctx) {
        free(epd);
        LOGF(ERR, "ssl ctx not inited");
        close(client_fd);
        return;
    }

    if(ssl_epd_idx == -1) {
        epd->ssl_verify = 1;
    }

    epd->L = new_lua_thread(_L);

    if(epd->L) {
        lua_pushlightuserdata(epd->L, epd);
        lua_setglobal(epd->L, "__epd__");
    }

    epd->fd = client_fd;
    epd->client_addr = client_addr;
    epd->status = STEP_WAIT;
    epd->content_length = -1;
    epd->keepalive = -1;
    epd->response_sendfile_fd = -1;
    //epd->start_time = longtime();

    epd->se_ptr = se_add(loop_fd, client_fd, epd);
    epd->timeout_ptr = add_timeout(epd, STEP_WAIT_TIMEOUT, timeout_handle);

    se_be_read(epd->se_ptr, _be_ssl_accept);

    serv_status.active_counts++;
    serv_status.connect_counts++;
}

void worker_main(int _worker_n)
{
    worker_n = _worker_n;
    attach_on_exit(on_exit_handler);

    if(is_daemon == 1 && !getarg("gcore")) {
        set_process_user(/*user*/ NULL, /*group*/ NULL);
    }

    if(!init_ntoa_cache() || !init_access_cache()) {
        LOGF(ERR, "Couldn't init rbtree");
        exit(1);
    }

    init_mime_types();
    shm_serv_status = _shm_serv_status->p;

    if(luaL_loadfile(_L, "core.lua")) {
        LOGF(ERR, "Couldn't load file: %s", lua_tostring(_L, -1));
        exit(1);
    }

    int mrkey = luaL_ref(_L, LUA_REGISTRYINDEX);

    lua_pushstring(_L, "__main");
    lua_rawgeti(_L, LUA_REGISTRYINDEX, mrkey);
    lua_rawset(_L, LUA_GLOBALSINDEX);

    int thread_count = 1000;

    if(getarg("thread")) {
        thread_count = atoi(getarg("thread"));

        if(thread_count < 10) {
            thread_count = 10;

        } else if(thread_count > 10000) {
            thread_count = 10000;
        }
    }

    init_lua_threads(_L, thread_count);

    /// 进入 loop 处理循环
    loop_fd = se_create(4096);
    set_loop_fd(loop_fd, _worker_n); // for coevent module
    se_accept(loop_fd, server_fd, be_accept);

    if(ssl_server_fd > 0) {
        se_accept(loop_fd, ssl_server_fd, be_ssl_accept);
    }

    se_loop(loop_fd, 10, other_simple_jobs); // loop

    exit(0);
}
