#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _TIMEOUTS_H
#define _TIMEOUTS_H

typedef void timeout_handle_t ( void *ptr );

typedef struct {
    timeout_handle_t *handle;
    void *ptr;
    time_t timeout;
    void *uper;
    void *next;
} timeout_t;

timeout_t *add_timeout ( void *ptr, int timeout, timeout_handle_t *handle );
int check_timeouts();
void delete_timeout ( timeout_t *n );

#endif // _TIMEOUTS_H
