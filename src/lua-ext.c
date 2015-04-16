#include <sys/stat.h>

#include "config.h"
#include "main.h"
#include "network.h"
#include "lua-ext.h"

static char temp_buf_1024[1024] = {0};
static char temp_buf[8192];
static char temp_buf2[8192];

static epdata_t *get_epd(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    return epd;
}

int lua_check_timeout(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->websocket) {
        return 0;
    }

    if(longtime() - epd->start_time > STEP_PROCESS_TIMEOUT) {
        epd->keepalive = 0;
        LOGF(ERR, "Process Time Out!");
        lua_pushstring(L, "Process Time Out!");
        lua_error(L);    /// stop lua script
    }

    return 0;
}

int lua_header(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->websocket) {
        return 0;
    }

    if(lua_gettop(L) < 1) {
        return 0;
    }

    if(epd->header_sended != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "respone header has been sended");
        return 2;
    }

    int t = lua_type(L, 1);
    size_t dlen = 0;
    const char *data = NULL;
    int ret = 0;

    if(t == LUA_TSTRING) {
        data = lua_tolstring(L, 1, &dlen);

        if(stristr(data, "content-length", dlen) != data) {
            ret = network_send_header(epd, data);
        }

    } else if(t == LUA_TTABLE) {
        int len = lua_objlen(L, 1), i = 0;

        for(i = 0; i < len; i++) {
            lua_pushinteger(L, i + 1);
            lua_gettable(L, -2);

            if(lua_isstring(L, -1)) {
                data = lua_tolstring(L, -1, &dlen);

                if(stristr(data, "content-length", dlen) != data) {
                    ret = network_send_header(epd, lua_tostring(L, -1));
                }
            }

            lua_pop(L, 1);
        }
    }

    if(ret == -1) {
        lua_pushnil(L);
        lua_pushstring(L, "respone header too big");
        return 2;

    } else if(ret == 0) {
        lua_pushnil(L);

    } else {
        lua_pushboolean(L, 1);
    }

    return 1;
}

static int send_then_send(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;
    epd->next_proc = NULL;

    char *buf = epd->next_out;
    int len = epd->next_out_len;
    epd->next_out_len = 0;

    int have = network_send(epd, buf, len);

    if(have > 0) {
        epd->next_out = malloc(have);

        if(epd->next_out) {
            memcpy(epd->next_out, buf + (len - have), have);
            free(buf);

            if(network_flush(epd) == 1) {
                epd->next_proc = send_then_send;
                epd->next_out_len = have;
                return have;

            } else {
                LOGF(ERR, "flush error");
                free(epd->next_out);
                epd->next_out = NULL;
                return 0;
            }

            return 0;
        }
    }

    free(buf);

    lua_f_lua_uthread_resume_in_c(epd->L, 0);

    return 0;
}

static int _lua_echo(epdata_t *epd, lua_State *L, int nargs, int can_yield)
{
    size_t len = 0;
    int have = 0;
    epd->next_out = NULL;

    if(lua_istable(L, 1)) {
        len = lua_calc_strlen_in_table(L, 1, 2, 0 /* strict */);

        if(len < 1) {
            return 0;
        }

        char *buf = temp_buf;

        if(len > 8192) {
            buf = malloc(len);

            if(!buf) {
                return 0;
            }

            lua_copy_str_in_table(L, 1, buf);
            have = network_send(epd, buf, len);

            if(have > 0 && can_yield) {
                epd->next_out = malloc(have);
                memcpy(epd->next_out, buf + (len - have), have);
            }

            free(buf);

        } else {
            lua_copy_str_in_table(L, 1, buf);
            have = network_send(epd, buf, len);

            if(have > 0 && can_yield) {
                epd->next_out = malloc(have);
                memcpy(epd->next_out, buf + (len - have), have);
            }
        }

    } else {
        const char *data = NULL;
        int i = 0;

        for(i = 1; i <= nargs; i++) {
            if(lua_isboolean(L, i)) {
                char *buf = NULL;

                if(lua_toboolean(L, i)) {
                    buf = "true";
                    have = network_send(epd, buf, 4);

                } else {
                    buf = "false";
                    have = network_send(epd, buf, 5);
                }

                if(have > 0 && can_yield) {
                    epd->next_out = malloc(have);
                    memcpy(epd->next_out, buf + (len - have), have);
                }

            } else {
                data = lua_tolstring(L, i, &len);
                have = network_send(epd, data, len);

                if(have > 0 && can_yield) {
                    epd->next_out = malloc(have);
                    memcpy(epd->next_out, data + (len - have), have);
                }
            }
        }
    }

    if(epd->next_out) {
        if(network_flush(epd) == 1) {
            epd->next_proc = send_then_send;
            epd->next_out_len = have;
            return have;

        } else {
            LOGF(ERR, "flush error");
            free(epd->next_out);
            epd->next_out = NULL;
            return 0;
        }
    }

    return 0;
}

int lua_echo(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    int nargs = lua_gettop(L);

    if(nargs < 1) {
        luaL_error(L, "miss content!");
        return 0;
    }

    size_t len = 0;

    if(epd->websocket) {
        return 0;
    }

    if(_lua_echo(epd, L, nargs, 1)) {
        return lua_yield(L, 0);
    }

    if(longtime() - epd->start_time > STEP_PROCESS_TIMEOUT) {
        epd->keepalive = 0;
        lua_pushstring(L, "Process Time Out!");
        lua_error(L);    /// stop lua script
    }

    return 0;
}

int lua_print_error(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    lua_gc(L, LUA_GCCOLLECT, 0);

    lua_Debug ar;
    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "nSl", &ar);

    if(!ar.source) {
        return 0;
    }

    snprintf(temp_buf_1024, 1024, "%s:%d", ar.source + 1, ar.currentline);

    lua_getfield(L, LUA_GLOBALSINDEX, "debug");

    if(!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }

    lua_getfield(L, -1, "traceback");

    if(!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }

    lua_pushvalue(L, 1);  /* pass error message */
    lua_pushinteger(L, 2);  /* skip this function and traceback */
    lua_call(L, 2, 1);  /* call debug.traceback */

    size_t len = 0;
    char *msg = (char *)lua_tolstring(L, -1, &len);
    int i = 0;
    memcpy(temp_buf2, "<h3>Error: ", 11);
    int j = 11;
    int is_first_line = 1;

    for(i = 0; i < len; i++) {
        temp_buf[i] = (msg[i] != '\n' ? msg[i] : ' ');

        if(is_first_line && msg[i] == '\n') {
            memcpy(temp_buf2 + j, "</h3>\n<pre>", 11);
            j += 11;
        }

        temp_buf2[j++] = msg[i];
    }

    temp_buf[len] = '\0';
    memcpy(temp_buf2 + j, "\n</pre>", 7);
    j += 7;
    temp_buf2[j] = '\0';

    _LOGF(ERR, temp_buf_1024, "%s", temp_buf);
    network_send(epd, temp_buf2, j);

    return 1;
}

int lua_clear_header(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->header_sended != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "respone header has been sended");
        return 2;
    }

    epd->response_header_length = 0;
    free(epd->iov[0].iov_base);
    epd->iov[0].iov_base = NULL;
    epd->iov[0].iov_len = 0;
    return 0;
}

#ifdef __APPLE__
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif
static const char *DAYS_OF_WEEK[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTHS_OF_YEAR[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static char _gmt_time[64] = {0};
int network_sendfile(epdata_t *epd, const char *path)
{
    if(epd->process_timeout == 1) {
        return 0;
    }

    struct stat st;

    if((epd->response_sendfile_fd = open(path, O_RDONLY)) < 0) {
        epd->response_sendfile_fd = -2;
        //printf ( "Can't open '%s' file\n", path );
        return 0;
    }

    if(fstat(epd->response_sendfile_fd, &st) == -1) {
        close(epd->response_sendfile_fd);
        epd->response_sendfile_fd = -2;
        //printf ( "Can't stat '%s' file\n", path );
        return 0;
    }

    epd->response_content_length = st.st_size;
    epd->response_buf_sended = 0;

    /// clear send bufs;!!!
    int i = 0;

    for(i = 1; i < epd->iov_buf_count; i++) {
        free(epd->iov[i].iov_base);
        epd->iov[i].iov_base = NULL;
        epd->iov[i].iov_len = 0;
    }

    epd->iov_buf_count = 0;

    struct tm *_clock;
    _clock = gmtime(&(st.st_mtime));
    sprintf(_gmt_time, "%s, %02d %s %04d %02d:%02d:%02d GMT",
            DAYS_OF_WEEK[_clock->tm_wday],
            _clock->tm_mday,
            MONTHS_OF_YEAR[_clock->tm_mon],
            _clock->tm_year + 1900,
            _clock->tm_hour,
            _clock->tm_min,
            _clock->tm_sec);

    if(epd->if_modified_since && strcmp(_gmt_time, epd->if_modified_since) == 0) {
        epd->response_header_length = 0;
        free(epd->iov[0].iov_base);
        epd->iov[0].iov_base = NULL;
        epd->iov[0].iov_len = 0;
        network_send_header(epd, "HTTP/1.1 304 Not Modified");
        close(epd->response_sendfile_fd);
        epd->response_sendfile_fd = -1;
        epd->response_content_length = 0;
        return 1;
    }

    if(epd->iov[0].iov_base == NULL || !stristr(epd->iov[0].iov_base, "content-type:", epd->response_header_length)) {
        sprintf(temp_buf, "Content-Type: %s", get_mime_type(path));
        network_send_header(epd, temp_buf);
    }

    sprintf(temp_buf, "Last-Modified: %s", _gmt_time);
    network_send_header(epd, temp_buf);

    if(temp_buf[14] == 't' && temp_buf[15] == 'e') {
        int fd = epd->response_sendfile_fd;
        epd->response_sendfile_fd = -1;
        epd->response_content_length = 0;
        int n = 0;

        while((n = read(fd, &temp_buf, 4096)) > 0) {
            network_send(epd, temp_buf, n);
        }

        if(n < 0) {
        }

        close(fd);

        return 1;
    }

    /*
    #ifdef linux
        int set = 1;
        setsockopt(epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof(int));
    #endif
    */
    return 1;
}

int lua_sendfile(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->header_sended != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "respone header has been sended");
        return 2;
    }

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    size_t len = 0;
    const char *fname = lua_tolstring(L, 1, &len);
    //char *full_fname = malloc(epd->vhost_root_len + len);
    char *full_fname = (char *)&temp_buf;
    memcpy(full_fname, epd->vhost_root, epd->vhost_root_len);
    memcpy(full_fname + epd->vhost_root_len , fname, len);
    full_fname[epd->vhost_root_len + len] = '\0';

    network_sendfile(epd, full_fname);

    //free(full_fname);

    lua_pushnil(L);
    lua_error(L); /// stop lua script

    network_be_end(epd);

    return 0;
}

int lua_end(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        return 0;
    }

    if(epd->status != STEP_PROCESS) {
        return 0;
    }

    if(epd->websocket || epd->status == STEP_SEND) {
        if(epd->websocket) {
            epd->websocket->ended = 1;
        }

        return 0;
    }

    network_be_end(epd);
    return 0;
}

int lua_die(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        return 0;
    }

    if(epd->websocket || epd->status == STEP_SEND) {
        if(epd->websocket) {
            epd->websocket->ended = 1;
        }

        return 0;
    }

    int nargs = lua_gettop(L);

    _lua_echo(epd, L, nargs, 0);

    if(epd->status != STEP_PROCESS) {
        return 0;
    }

    lua_pushnil(L);
    lua_error(L); /// stop lua script

    //network_be_end(epd);

    return 0;
}

int lua_flush(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        return 0;
    }

    int nargs = lua_gettop(L);
    _lua_echo(epd, L, nargs, 0);

    if(epd->status != STEP_PROCESS) {
        return 0;
    }

    if(epd->websocket || epd->status == STEP_SEND) {
        return 0;
    }

    if(network_flush(epd) == 1) {
        return lua_yield(L, 0);
    }

    return 0;
}

static int network_be_read_request_body(se_ptr_t *ptr)
{
    //printf("network_be_read_request_body\n");
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0, readed = 0;
    char *buf = malloc(65536);
    int buf_size = 65536;

    if(!buf) {
        serv_status.active_counts--;

        se_delete(epd->se_ptr);
        epd->se_ptr = NULL;
        close(epd->fd);
        epd->fd = -1;

        if(epd->status == STEP_READ) {
            serv_status.reading_counts--;
            epd->status = STEP_PROCESS;
        }

        lua_pushnil(epd->L);
        lua_pushstring(epd->L, "memory error");
        LOGF(ERR, "memory error!");

        lua_f_lua_uthread_resume_in_c(epd->L, 2);

        return 0;
    }

    while((n = recv(epd->fd, buf + readed, buf_size - readed, 0)) >= 0) {
        if(n == 0) {
            serv_status.active_counts--;

            se_delete(epd->se_ptr);
            epd->se_ptr = NULL;
            close(epd->fd);
            epd->fd = -1;

            if(epd->status == STEP_READ) {
                serv_status.reading_counts--;
                epd->status = STEP_PROCESS;
            }

            break;
        }

        epd->data_len += n;
        readed += n;

        //printf("readed: %d\n", n);
        if(readed >= buf_size) {
            char *p = realloc(buf, buf_size + 65536);

            if(p) {
                buf = p;
                buf_size += 65536;

            } else {
                break;
            }
        }
    }

    if(readed > 0) {
        if(epd->status == STEP_READ) {
            serv_status.reading_counts--;
            epd->status = STEP_PROCESS;
        }

        se_be_pri(epd->se_ptr, NULL); // be wait
        lua_pushlstring(epd->L, buf, readed);
        free(buf);

        lua_f_lua_uthread_resume_in_c(epd->L, 1);

    } else if(n == 0) {
        n = -1;
        errno = 1;
    }

    if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        serv_status.active_counts--;

        se_delete(epd->se_ptr);
        epd->se_ptr = NULL;
        close(epd->fd);
        epd->fd = -1;

        if(epd->status == STEP_READ) {
            serv_status.reading_counts--;
            epd->status = STEP_PROCESS;
        }

        lua_pushnil(epd->L);
        lua_pushstring(epd->L, "socket closed");

        lua_f_lua_uthread_resume_in_c(epd->L, 2);

        return 0;
    }
}

int lua_read_request_body(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->websocket) {
        return 0;
    }

    if(epd->contents) {
        epd->contents = NULL;

        lua_pushlstring(L, epd->headers + epd->_header_length, epd->data_len - epd->_header_length);
        return 1;
    }

    if(!epd->se_ptr || epd->content_length <= epd->data_len - epd->_header_length) {
        lua_pushnil(L);
        lua_pushstring(L, "eof");
        return 2;
    }

    epd->status = STEP_READ;
    serv_status.reading_counts++;
    se_be_read(epd->se_ptr, network_be_read_request_body);

    return lua_yield(L, 0);
}

int lua_f_get_boundary(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(epd->boundary) {
        lua_pushstring(L, epd->boundary);

    } else {
        lua_pushnil(L);
    }

    return 1;
}

int lua_f_random_string(lua_State *L)
{
    int size = 32;

    if(lua_gettop(L) == 1 && lua_isnumber(L, 1)) {
        size = lua_tonumber(L, 1);
    }

    if(size < 1) {
        size = 32;
    }

    if(size > 4096) {
        return 0;
    }

    random_string(temp_buf, size, 0);

    lua_pushlstring(L, temp_buf, size);

    return 1;
}

int lua_f_file_exists(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    size_t len = 0;
    const char *fname = lua_tolstring(L, 1, &len);
    char *full_fname = (char *)&temp_buf;
    memcpy(full_fname, epd->vhost_root, epd->vhost_root_len);
    memcpy(full_fname + epd->vhost_root_len, fname, len);
    full_fname[epd->vhost_root_len + len] = '\0';

    lua_pushboolean(L, cached_access(fnv1a_32(full_fname, epd->vhost_root_len + len), full_fname) != -1);

    return 1;
}

int lua_f_readfile(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(!lua_isstring(L, -1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    size_t len = 0;
    const char *fname = lua_tolstring(L, 1, &len);
    char *full_fname = (char *)&temp_buf;
    memcpy(full_fname, epd->vhost_root, epd->vhost_root_len);
    memcpy(full_fname + epd->vhost_root_len , fname, len);
    full_fname[epd->vhost_root_len + len] = '\0';

    char *buf = NULL;
    off_t reads = 0;
    int fd = open(full_fname, O_RDONLY, 0);

    if(fd > -1) {
        reads = lseek(fd, 0L, SEEK_END);
        lseek(fd, 0L, SEEK_SET);

        if(reads > 8192) {
            buf = malloc(reads);

        } else {
            buf = (char *)&temp_buf;
        }

        read(fd, buf, reads);

        close(fd);

        lua_pushlstring(L, buf, reads);

        if(buf != (char *)&temp_buf) {
            free(buf);
        }

        return 1;
    }

    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));

    return 2;
}

int lua_f_filemtime(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(!lua_isstring(L, -1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    size_t len = 0;
    const char *fname = lua_tolstring(L, 1, &len);
    char *full_fname = (char *)&temp_buf;
    memcpy(full_fname, epd->vhost_root, epd->vhost_root_len);
    memcpy(full_fname + epd->vhost_root_len , fname, len);
    full_fname[epd->vhost_root_len + len] = '\0';

    struct stat fst;

    if(stat(full_fname, &fst) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    lua_pushnumber(L, fst.st_mtime);
    return 1;
}
