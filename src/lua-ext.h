#ifndef _ALILUA_EXT_H
#define _ALILUA_EXT_H

typedef struct {
    void *L;
    long timeout;
    void *uper;
    void *next;
} sleep_timeout_t;

int check_lua_sleep_timeouts();
int lua_f_sleep(lua_State *L);
int lua_check_timeout(lua_State *L);
int lua_header(lua_State *L);
int lua_echo(lua_State *L);
int lua_clear_header(lua_State *L);
int lua_sendfile(lua_State *L);
int lua_end(lua_State *L);
int lua_die(lua_State *L);
int lua_get_post_body(lua_State *L);
int lua_f_random_string(lua_State *L);
int lua_f_file_exists(lua_State *L);
int lua_f_readfile(lua_State *L);

#endif // _ALILUA_EXT_H
