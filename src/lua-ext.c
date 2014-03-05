#include <sys/stat.h>

#include "main.h"
#include "network.h"
#include "lua-ext.h"

static char temp_buf[8192];

static sleep_timeout_t *timeout_links[64] = {0};
static sleep_timeout_t *timeout_link_ends[64] = {0};
static time_t now_4sleep;
int check_lua_sleep_timeouts()
{
    time(&now_4sleep);
    int k = now_4sleep % 64;

    sleep_timeout_t *m = timeout_links[k], *n = NULL;
    lua_State *L = NULL;

    while(m) {
        n = m;
        m = m->next;

        if(now_4sleep >= n->timeout) { // timeout
            {
                if(n->uper) {
                    ((sleep_timeout_t *) n->uper)->next = n->next;

                } else {
                    timeout_links[k] = n->next;
                }

                if(n->next) {
                    ((sleep_timeout_t *) n->next)->uper = n->uper;

                } else {
                    timeout_link_ends[k] = n->uper;
                }

                L = n->L;
                free(n);
            }
            if(L)
            lua_resume(L, 0);
            L = NULL;
        }
    }

    return 1;
}

int lua_f_sleep(lua_State *L)
{
    if(!lua_isnumber(L, 1)) {
        return 0;
    }

    int sec = lua_tonumber(L, 1);

    if(sec < 1) {
        return 0;
    }

    if(sec > 1000000) {
        return lua_yield(L, 0);       // for ever
    }

    time(&now_4sleep);

    sleep_timeout_t *n = malloc(sizeof(sleep_timeout_t));

    if(!n) {
        return 0;
    }

    n->timeout = now_4sleep + sec;
    n->uper = NULL;
    n->next = NULL;
    n->L = L;

    int k = n->timeout % 64;

    if(timeout_link_ends[k] == NULL) {
        timeout_links[k] = n;
        timeout_link_ends[k] = n;

    } else { // add to link end
        timeout_link_ends[k]->next = n;
        n->uper = timeout_link_ends[k];
        timeout_link_ends[k] = n;
    }

    return lua_yield(L, 0);
}

int lua_check_timeout(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket) {
        return 0;
    }

    if(epd->process_timeout == 1) {
        epd->keepalive = 0;
        network_be_end(epd);
        lua_pushstring(L, "Process Time Out!");
        lua_error(L);    /// stop lua script
    }

    return 0;
}

int lua_header(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket) {
        return 0;
    }

    if(lua_gettop(L) < 2) {
        return 0;
    }

    int t = lua_type(L, 2);
    size_t dlen = 0;
    const char *data = NULL;

    if(t == LUA_TSTRING) {
        data = lua_tolstring(L, 2, &dlen);

        if(stristr(data, "content-length", dlen) != data) {
            network_send_header(epd, data);
        }

    } else if(t == LUA_TTABLE) {
        int len = lua_objlen(L, 2), i = 0;

        for(i = 0; i < len; i++) {
            lua_pushinteger(L, i + 1);
            lua_gettable(L, -2);

            if(lua_isstring(L, -1)) {
                data = lua_tolstring(L, -1, &dlen);

                if(stristr(data, "content-length", dlen) != data) {
                    network_send_header(epd, lua_tostring(L, -1));
                }
            }

            lua_pop(L, 1);
        }
    }

    return 0;
}

int lua_echo(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    int nargs = lua_gettop(L);

    if(nargs < 2) {
        luaL_error(L, "miss content!");
        return 0;
    }

    size_t len = 0;
    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket) {
        return 0;
    }

    if(lua_istable(L, 2)) {
        len = lua_calc_strlen_in_table(L, 2, 2, 0 /* strict */);

        if(len < 1) {
            return 0;
        }

        char *buf = temp_buf;

        if(len > 4096) {
            buf = malloc(len);

            if(!buf) {
                return 0;
            }

            lua_copy_str_in_table(L, 2, buf);
            network_send(epd, buf, len);
            free(buf);

        } else {
            lua_copy_str_in_table(L, 2, buf);
            network_send(epd, buf, len);
        }

    } else {
        const char *data = NULL;
        int i = 0;

        for(i = 2; i <= nargs; i++) {
            if(lua_isboolean(L, i)) {
                if(lua_toboolean(L, i)) {
                    network_send(epd, "true", 4);

                } else {
                    network_send(epd, "false", 5);
                }

            } else {
                data = lua_tolstring(L, i, &len);
                network_send(epd, data, len);
            }
        }
    }

    return 0;
}

int lua_clear_header(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);
    epd->response_header_length = 0;
    free(epd->iov[0].iov_base);
    epd->iov[0].iov_base = NULL;
    epd->iov[0].iov_len = 0;
    return 0;
}

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

    sprintf(temp_buf, "Content-Type: %s", get_mime_type(path));
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

#ifdef linux
    int set = 1;
    setsockopt(epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof(int));
#endif
    return 1;
}

int lua_sendfile(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket) {
        return 0;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    network_sendfile(epd, lua_tostring(L, 2));

    return 0;
}

int lua_die(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket || epd->status == STEP_SEND) {
        return 0;
    }

    network_be_end(epd);
    lua_pushnil(L);
    lua_error(L);    /// stop lua script
    return 0;
}

int lua_get_post_body(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        luaL_error(L, "miss epd!");
        return 0;
    }

    epdata_t *epd = lua_touserdata(L, 1);

    if(epd->websocket) {
        return 0;
    }

    if(epd->content_length > 0) {
        lua_pushlstring(L, epd->contents, epd->content_length);

    } else {
        lua_pushnil(L);
    }

    epd->contents = NULL;
    epd->content_length = 0;

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
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    const char *fname = lua_tostring(L, 1);

    lua_pushboolean(L, access(fname, F_OK) != -1);

    return 1;
}
