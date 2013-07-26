#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/epoll.h>

#ifndef _SE_H
#define _SE_H

#define SE_SIZE 1024

typedef struct {
    int epoll_fd;
    int fd;
    void *func;
    void *data;
    long z; /// fix size to 32
} se_ptr_t;

typedef int se_rw_proc_t ( se_ptr_t *ptr );
typedef int se_waitout_proc_t ( );

int se_create ( int event_size );
static struct epoll_event events[SE_SIZE], ev;
int se_loop ( int epoll_fd, int waitout, se_waitout_proc_t *waitout_proc );
se_ptr_t *se_add ( int epoll_fd, int fd, void *data );
int se_delete ( se_ptr_t *ptr );
int se_be_read ( se_ptr_t *ptr, se_rw_proc_t *func );
int se_be_write ( se_ptr_t *ptr, se_rw_proc_t *func );
int se_be_pri ( se_ptr_t *ptr, se_rw_proc_t *func );


#endif // _SE_H
