#include "../coevent/merry/merry.h"
#include <zlib.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifndef _ALILUA_MAIN_H
#define _ALILUA_MAIN_H

#define STEP_READ 1
#define STEP_JOIN_PROCESS 2
#define STEP_PROCESS 3
#define STEP_SEND 4
#define STEP_FINISH 5
#define STEP_WAIT 6
#define STEP_ASYNC 7
#define STEP_ERROR 8

shm_t *_shm_serv_status;

#endif /// _ALILUA_MAIN_H
