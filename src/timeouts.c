#include "timeouts.h"


static time_t now = 0;

#define NULL8 NULL, NULL, NULL, NULL,NULL, NULL, NULL, NULL,
static timeout_t *timeout_links[64] = {NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8};
static timeout_t *timeout_link_ends[64] = {NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8 NULL8};
#define F1_8 -1,-1,-1,-1,-1,-1,-1,-1,
//static int timeouts[64] = {F1_8 F1_8 F1_8 F1_8 F1_8 F1_8 F1_8 F1_8};

timeout_t *add_timeout ( void *ptr, int timeout, timeout_handle_t *handle )
{
    //now = time ( NULL );

    if ( timeout < 1 ) {
        return NULL;
    }

    //timeouts[timeout % 64] = 1;

    timeout_t *n = malloc ( sizeof ( timeout_t ) );

    if ( !n ) {
        return NULL;
    }

    n->handle = handle;
    n->ptr = ptr;
    n->timeout = now + timeout;
    n->uper = NULL;
    n->next = NULL;

    int k = n->timeout % 64;

    if ( timeout_link_ends[k] == NULL ) {
        timeout_links[k] = n;
        timeout_link_ends[k] = n;

    } else { // add to link end
        timeout_link_ends[k]->next = n;
        n->uper = timeout_link_ends[k];
        timeout_link_ends[k] = n;
    }

    return n;
}

int check_timeouts()
{
    now = time ( NULL );

    int k = now % 64;

    timeout_t *m = timeout_links[k], *n = NULL;

    while ( m ) {
        n = m;
        m = m->next;

        if ( now >= n->timeout ) { // timeout
            n->handle ( n->ptr );

            //delete_timeout ( n );
        }
    }

    return 1;
}

void delete_timeout ( timeout_t *n )
{
    if ( !n ) {
        return;
    }

    int k = n->timeout % 64;

    if ( n->uper ) {
        ( ( timeout_t * ) n->uper )->next = n->next;

    } else {
        timeout_links[k] = n->next;
    }

    if ( n->next ) {
        ( ( timeout_t * ) n->next )->uper = n->uper;

    } else {
        timeout_link_ends[k] = n->uper;
    }

    free ( n );
}

void update_timeout ( timeout_t *n, int timeout )
{
    //now = time ( NULL );

    if ( !n ) {
        return;
    }

    int k = n->timeout % 64;

    if ( n->uper ) {
        ( ( timeout_t * ) n->uper )->next = n->next;

    } else {
        timeout_links[k] = n->next;
    }

    if ( n->next ) {
        ( ( timeout_t * ) n->next )->uper = n->uper;

    } else {
        timeout_link_ends[k] = n->uper;
    }

    if ( timeout < 1 ) {
        free ( n );
        return;
    }

    n->timeout = now + timeout;
    n->uper = NULL;
    n->next = NULL;

    k = n->timeout % 64;

    if ( timeout_link_ends[k] == NULL ) {
        timeout_links[k] = n;
        timeout_link_ends[k] = n;

    } else { // add to link end
        timeout_link_ends[k]->next = n;
        n->uper = timeout_link_ends[k];
        timeout_link_ends[k] = n;
    }
}
