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

    struct timespec tmout = { 0,     /* block for 5 seconds at most */ 
                         waitout  };   /* nanoseconds */
                          
    while ( 1 ) {
        if ( waitout_proc ) {
            r = waitout_proc();
        }

        //n = epoll_wait ( loop_fd, events, SE_SIZE, waitout );
        n = kevent(loop_fd, NULL, 0, events, SE_SIZE, &tmout);

        for ( i = 0; i < n; i++ ) {
            ptr = events[i].data;

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


    //ev.data.ptr = ptr;
    //ev.events = EPOLLPRI;

    //int ret = epoll_ctl ( loop_fd, EPOLL_CTL_ADD, fd, &ev );
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, ptr);
    int ret = kevent(loop_fd, ev, 1, NULL, 0, NULL);

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

    /*if ( epoll_ctl ( ptr->loop_fd, EPOLL_CTL_DEL, ptr->fd, &ev ) < 0 ) {
        return -1;
    }*/
    EV_SET(&ev, ptr->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    if ( kevent(ptr->loop_fd, &ev, 1, NULL, 0, NULL) < 0){
        return -1;
    }

    free ( ptr );

    return 0;
}

int se_be_read ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    //ev.data.ptr = ptr;
    //ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
    EV_SET(&ev, ptr->fd, EVFILT_READ, EV_ENABLE, 0, 0, ptr);
    return kevent(ptr->loop_fd, ev, 1, NULL, 0, NULL);
    //return epoll_ctl ( ptr->loop_fd, EPOLL_CTL_MOD, ptr->fd, &ev );
}

int se_be_write ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    EV_SET(&ev, ptr->fd, EVFILT_WRITE, EV_ENABLE, 0, 0, ptr);
    return kevent(ptr->loop_fd, ev, 1, NULL, 0, NULL);
}

int se_be_pri ( se_ptr_t *ptr, se_rw_proc_t *func )
{
    ptr->func = func;

    EV_SET(&ev, ptr->fd, EVFILT_WRITE, EV_DISABLE, 0, 0, ptr);
    return kevent(ptr->loop_fd, ev, 1, NULL, 0, NULL);
}

#endif
