#ifndef _CONFIG_H
#define _CONFIG_H

#define ALILUA_VERSION "v0.52"
#define default_port 19827

#define USE_KEEPALIVE 1

/// timeouts (msec)
#define STEP_READ_TIMEOUT 30000
#define STEP_JOIN_PROCESS_TIMEOUT 30000
#define STEP_PROCESS_TIMEOUT 60000
#define STEP_SEND_TIMEOUT 30000
#define STEP_FINISH_TIMEOUT 60000
#define STEP_WAIT_TIMEOUT 60000
#define STEP_WS_READ_TIMEOUT 10000 // for websocket
#define STEP_WS_SEND_TIMEOUT 30000

#define EPD_POOL_SIZE 4096
#define EPOLL_WAITOUT 10

/// others
#define GZIP_LEVEL 2

#endif /// _CONFIG_H
