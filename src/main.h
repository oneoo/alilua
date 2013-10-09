#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/timeb.h>
#include <math.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <zlib.h>

#include <lua.h>
#include <lauxlib.h>

#include "config.h"
#include "../common/process.h"
#include "network.h"

#ifndef _MAIN_H
#define _MAIN_H

#define large_malloc(s) (malloc(((int)(s/4096)+1)*4096))
#define PRINTF(a,args...) if(1>0)fprintf( stdout, "%s:%d:"#a"\n", __FUNCTION__, __LINE__, ##args)
#define LOGF(a,args...) if(LOG_FD)fprintf( LOG_FD, "%s:%d:"#a"\n", __FUNCTION__, __LINE__, ##args)

#define A_C_R "\x1b[31m"
#define A_C_G "\x1b[32m"
#define A_C_Y "\x1b[33m"
#define A_C_B "\x1b[34m"
#define A_C_M "\x1b[35m"
#define A_C_C "\x1b[36m"
#define A_C__ "\x1b[0m"

#define cr_printf(fmt, ...) printf("%s" fmt "%s", A_C_R, ##__VA_ARGS__, A_C__)
#define cg_printf(fmt, ...) printf("%s" fmt "%s", A_C_G, ##__VA_ARGS__, A_C__)
#define cy_printf(fmt, ...) printf("%s" fmt "%s", A_C_Y, ##__VA_ARGS__, A_C__)
#define cb_printf(fmt, ...) printf("%s" fmt "%s", A_C_B, ##__VA_ARGS__, A_C__)
#define cm_printf(fmt, ...) printf("%s" fmt "%s", A_C_M, ##__VA_ARGS__, A_C__)
#define cc_printf(fmt, ...) printf("%s" fmt "%s", A_C_C, ##__VA_ARGS__, A_C__)

char hostname[1024];

int lua_errorlog ( lua_State *L );
int lua_check_timeout ( lua_State *L );
int lua_header ( lua_State *L );
int lua_echo ( lua_State *L );
int lua_clear_header ( lua_State *L );
int lua_sendfile ( lua_State *L );
int lua_die ( lua_State *L );
int lua_get_post_body ( lua_State *L );
int lua_f_cache_set ( lua_State *L );
int lua_f_cache_get ( lua_State *L );
int lua_f_cache_del ( lua_State *L );
int lua_f_random_string ( lua_State *L );
int lua_f_file_exists ( lua_State *L );
int lua_f_is_websocket ( lua_State *L );
int lua_f_upgrade_to_websocket ( lua_State *L );
int lua_f_websocket_send ( lua_State *L );
int check_lua_sleep_timeouts();
int lua_f_sleep ( lua_State *L );

char *_process_chdir;

#endif
