#include "../coevent/merry/merry.h"
#include "../coevent/merry/common/rbtree.h"
#include <netinet/in.h>

#ifndef _ALILUA_CACHED_NTOA_H
#define _ALILUA_CACHED_NTOA_H

typedef struct {
    unsigned long key;
    char addr[48];
} rb_ntoa_key_t;

int init_ntoa_cache();
const char *cached_ntoa(struct in_addr addr);

#endif