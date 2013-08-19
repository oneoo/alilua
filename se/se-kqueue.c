#include "se.h"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(BSD)
#include <sys/event.h>
struct kevent events[SE_SIZE], ev;

int se_create ( int event_size )
{
    return kqueue ( );
}

int se_loop ( int loop_fd, int waitout, se_waitout_proc_t *waitout_proc )
{
    int n = 0, i = 0, r = 1;
    se_ptr_t *ptr = NULL;

    struct timespec tmout = { waitout / 1000, waitout % 1000 };

    while ( 1 ) {
        if ( waitout_proc ) {
            r = waitout_proc();
        }

        n = kevent ( loop_fd, NULL, 0, events, SE_SIZE, &tmout );

        for ( i = 0; i < n; i++ ) {
            ptr = events[i].udata;

            if ( ptr->func ) {
                ( ( se_rw_proc_t * ) ptr->func ) ( ptr );
            }
        }

        if ( !r || n == -1 ) {
            break;
        }

    }
}

se_ptr_t *se_add ( int loop_fd, int fd, void *data )
{
    se_ptr_t *ptr = malloc ( sizeof ( se_ptr_t ) );

    if ( !ptr ) {
        return ptr;
    }

    ptr->loop_fd = loop_fd;
    ptr->fd = fd;
    ptr->func = NULL;
    ptr->data = data;

    int ret = 0;
    EV_SET ( &ev, fd, EVFILT_READ, EV_ADD, 0, 0, ptr );
    ret = kevent ( loop_fd, &ev, 1, NULL, 0, NULL );
    EV_SET ( &ev, fd, EVFILT_READ, EV_DISABLE, 0, 0, ptr );
    ret = kevent ( loop_fd, &ev, 1, NULL, 0, NULL );

    if ( ret < 0 ) {
        free ( ptr );
        ptr = NULL;
    }

    EV_SET ( &ev, fd, EVFILT_WRITE, EV_ADD, 0, 0, ptr );
    ret = kevent ( loop_fd, &ev, 1, NULL, 0, NULL );
    EV_SET ( &ev, fd, EVFILT_WRITE, EV_DISABLE, 0, 0, ptr );
    ret = kevent ( loop_fd, &ev, 1, NULL, 0, NULL );


    if ( ret < 0 ) {
        free ( ptr );
        ptr = NULL;
    }

    EV_SET ( &ev, fd, EVFILT_SIGNAL, EV_ADD, 0, 0, ptr );

    ptr->mask = EVFILT_SIGNAL;
    ret = kevent ( loop_fd, &ev, 1, NULL, 0, NULL );

    if ( ret < 0 ) {
        free ( ptr );
        ptr = NULL;
    }

    return ptr;
}

int se_delete ( se_ptr_t *ptr )
{
    if ( !ptr ) {
        return -1;
    }

    EV_SET ( &ev, ptr->fd, EVFILT_READ, EV_DELETE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );
    EV_SET ( &ev, ptr->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );
    EV_SET ( &ev, ptr->fd, EVFILT_SIGNAL, EV_DELETE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );

    free ( ptr );

    return 0;
}

int se_be_read ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    EV_SET ( &ev, ptr->fd, ptr->mask, EV_DISABLE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );

    ptr->mask = EVFILT_READ;
    EV_SET ( &ev, ptr->fd, EVFILT_READ, EV_ENABLE, 0, 0, ptr );
    return kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );
}

int se_be_write ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    EV_SET ( &ev, ptr->fd, ptr->mask, EV_DISABLE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );

    ptr->mask = EVFILT_WRITE;
    EV_SET ( &ev, ptr->fd, EVFILT_WRITE, EV_ENABLE, 0, 0, ptr );
    return kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );
}

int se_be_pri ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    EV_SET ( &ev, ptr->fd, ptr->mask, EV_DISABLE, 0, 0, ptr );
    kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );

    ptr->mask = EVFILT_SIGNAL;
    EV_SET ( &ev, ptr->fd, EVFILT_SIGNAL, EV_ENABLE, 0, 0, ptr );
    return kevent ( ptr->loop_fd, &ev, 1, NULL, 0, NULL );
}

#endif
