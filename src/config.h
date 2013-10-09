#ifndef _CONFIG_H
#define _CONFIG_H

#define version "v0.30"
#define default_port 19827

#define USE_KEEPALIVE 1

/// timeouts
#define STEP_READ_TIMEOUT 30
#define STEP_JOIN_PROCESS_TIMEOUT 30
#define STEP_PROCESS_TIMEOUT 60
#define STEP_SEND_TIMEOUT 30
#define STEP_FINISH_TIMEOUT 60
#define STEP_WAIT_TIMEOUT 30
#define STEP_WS_READ_TIMEOUT 10 // for websocket
#define STEP_WS_SEND_TIMEOUT 60

/// poll config
#define EP_D_BUF_SIZE 4096 /// read block size
#define EPD_POOL_SIZE 4096*128
#define EPOLL_WAITOUT 10

/// yac config
#ifdef linux
#define YAC_KEY_DATA_SIZE ( 1024 * 1024 * 2 )
#define YAC_VALUE_DATA_SIZE ( 1024 * 1024 * 32 )
#else /// for mac os
#define YAC_KEY_DATA_SIZE ( 1024 * 128 )
#define YAC_VALUE_DATA_SIZE ( 1024 * 1024 * 2 )
#endif
/// others
#define GZIP_LEVEL 2

#endif /// _CONFIG_H