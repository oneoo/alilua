#ifndef _ALILUA_EXT_H
#define _ALILUA_EXT_H

int lua_check_timeout(lua_State *L);
int lua_header(lua_State *L);
int lua_echo(lua_State *L);
int lua_print_error(lua_State *L);
int lua_clear_header(lua_State *L);
int lua_sendfile(lua_State *L);
int lua_end(lua_State *L);
int lua_die(lua_State *L);
int lua_flush(lua_State *L);
int lua_read_request_body(lua_State *L);
int lua_f_get_boundary(lua_State *L);
int lua_f_random_string(lua_State *L);
int lua_f_file_exists(lua_State *L);
int lua_f_readfile(lua_State *L);
int lua_f_filemtime(lua_State *L);

int lua_f_router(lua_State *L);
int lua_f_package_require(lua_State *L);

#endif // _ALILUA_EXT_H
