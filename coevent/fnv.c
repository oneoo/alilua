#include "coevent.h"

uint32_t fnv1a_32 ( const char *data, uint32_t len )
{
    uint32_t rv = 0x811c9dc5U;
    uint32_t i = 0;

    for ( i = 0; i < len; i++ ) {
        rv = ( rv ^ ( unsigned char ) data[i] ) * 16777619;
    }

    return rv;
}

uint32_t fnv1a_64 ( const char *data, uint32_t len )
{
    uint64_t rv = 0xcbf29ce484222325UL;
    uint32_t i = 0;

    for ( i = 0; i < len; i++ ) {
        rv = ( rv ^ ( unsigned char ) data[i] ) * 1099511628211UL;
    }

    return ( uint32_t ) rv;
}
