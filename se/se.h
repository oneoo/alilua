#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>

#ifndef _SE_H
#define _SE_H

#define SE_SIZE 4096

typedef struct {
    int loop_fd;
    int fd;
    void *func;
    void *data;
    long z; /// fix size to 32
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(BSD)
    int mask;
#endif
} se_ptr_t;

typedef int se_rw_proc_t ( se_ptr_t *ptr );
typedef int se_waitout_proc_t ( );

int se_create ( int event_size );
int se_loop ( int loop_fd, int waitout, se_waitout_proc_t *waitout_proc );
se_ptr_t *se_add ( int loop_fd, int fd, void *data );
int se_delete ( se_ptr_t *ptr );
int se_be_read ( se_ptr_t *ptr, se_rw_proc_t *func );
int se_be_write ( se_ptr_t *ptr, se_rw_proc_t *func );
int se_be_pri ( se_ptr_t *ptr, se_rw_proc_t *func );


#endif // _SE_H
