#include "main.h"

#ifndef _VHOST_H
#define _VHOST_H

typedef struct vhost_conf_s {
    char host[256];
    int host_len;
    char root[1024];
    int mtype;
    struct vhost_conf_s *prev;
    struct vhost_conf_s *next;
} vhost_conf_t;

int update_vhost_routes(char *f);
vhost_conf_t *get_vhost_conf(char *host, int prefix);
char *get_vhost_root(char *host);

#endif
