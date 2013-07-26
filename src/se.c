#include "se.h"

int se_create ( int event_size )
{
    return epoll_create ( event_size );
}

int se_loop ( int epoll_fd, int waitout, se_waitout_proc_t *waitout_proc )
{
    int n = 0, numevs = 0;
    se_ptr_t *ptr = NULL;

    while ( 1 ) {
        numevs = epoll_wait ( epoll_fd, events, SE_SIZE, waitout );

        for ( n = 0; n < numevs; n++ ) {
            ptr = events[n].data.ptr;

            if ( ptr->func ) {
                ( ( se_rw_proc_t * ) ptr->func ) ( ptr );
            }
        }

        if ( waitout_proc && !waitout_proc() ) {
            break;
        }
    }
}

se_ptr_t *se_add ( int epoll_fd, int fd, void *data )
{
    se_ptr_t *ptr = malloc ( sizeof ( se_ptr_t ) );

    if ( !ptr ) {
        return ptr;
    }

    ptr->epoll_fd = epoll_fd;
    ptr->fd = fd;
    ptr->func = NULL;
    ptr->data = data;

    ev.data.fd = 0;
    ev.data.ptr = ptr;
    ev.events = EPOLLPRI;

    int ret = epoll_ctl ( epoll_fd, EPOLL_CTL_ADD, fd, &ev );

    if ( ret < 0 ) {
        free ( ptr );
        ptr = NULL;
    }

    return ptr;
}

int se_delete ( se_ptr_t *ptr )
{
    int ret = epoll_ctl ( ptr->epoll_fd, EPOLL_CTL_DEL, ptr->fd, &ev );

    if ( ret < 0 ) {
        return -1;
    }

    free ( ptr );

    return 0;
}

int se_be_read ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;
    ev.data.fd = 0;
    ev.data.ptr = ptr;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;

    return epoll_ctl ( ptr->epoll_fd, EPOLL_CTL_MOD, ptr->fd, &ev );
}

int se_be_write ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;
    ev.data.fd = 0;
    ev.data.ptr = ptr;
    ev.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP;

    return epoll_ctl ( ptr->epoll_fd, EPOLL_CTL_MOD, ptr->fd, &ev );
}

int se_be_pri ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;
    ev.data.fd = 0;
    ev.data.ptr = ptr;
    ev.events = EPOLLPRI;

    return epoll_ctl ( ptr->epoll_fd, EPOLL_CTL_MOD, ptr->fd, &ev );
}
