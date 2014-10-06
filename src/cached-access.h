#include "../coevent/merry/merry.h"
#include "../coevent/merry/common/rbtree.h"

#ifndef _ALILUA_CACHED_ACCESS_H
#define _ALILUA_CACHED_ACCESS_H

typedef struct {
    unsigned long key;
    unsigned long last;
    int exists;
} rb_access_key_t;

int init_access_cache();
int cached_access(unsigned long key, const char *path);

#endif