#include "main.h"
#include <openssl/md5.h>

#define PRIME32 16777619
static uint32_t fnv1a_32 ( const char *data, uint32_t len )
{
    uint32_t rv = 0x811c9dc5U;
    uint32_t i;

    for ( i = 0; i < len; i++ ) {
        rv = ( rv ^ ( unsigned char ) data[i] ) * PRIME32;
    }

    return rv;
}

static unsigned char digest[MD5_DIGEST_LENGTH];
static char md5chars[32 + 16];

int lua_f_cache_set ( lua_State *L )
{
    int nargs = lua_gettop ( L );

    if ( !lua_isstring ( L, 1 ) || nargs < 2 ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    size_t klen = 0;
    size_t vlen = 0;

    const char *key = lua_tolstring ( L, 1, &klen );

    if ( klen > 48 ) {
        memset ( digest, 0, MD5_DIGEST_LENGTH );
        MD5 ( ( unsigned char * ) key, klen, ( unsigned char * ) &digest );

        int i = 0;

        for ( i = 0; i < 16; i++ ) {
            sprintf ( &md5chars[i * 2], "%02x", ( unsigned int ) digest[i] );
        }

        klen = 32 + sprintf ( md5chars + 32, "-%ld", fnv1a_32 ( key, klen ) );

        key = md5chars;
    }

    int t = lua_type ( L, 2 );

    if ( klen < 1 || ( t != LUA_TSTRING && t != LUA_TNUMBER && t != LUA_TBOOLEAN ) ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    const char *value = lua_tolstring ( L, 2, &vlen );

    int ttl = 300;

    if ( nargs == 3 && lua_isnumber ( L, 3 ) ) {
        ttl = lua_tonumber ( L, 3 );

        if ( ttl < 0 ) {
            ttl = 0;
        }
    }

    lua_pushboolean ( L, yac_storage_update ( key, klen, value, vlen, 1, ttl, 0 ) );

    return 1;
}

int lua_f_cache_get ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    size_t klen = 0;
    size_t vlen = 0;
    const char *key = lua_tolstring ( L, 1, &klen );

    if ( klen < 1 ) {
        lua_pushnil ( L );
        return 1;
    }

    if ( klen > 48 ) {
        memset ( digest, 0, MD5_DIGEST_LENGTH );
        MD5 ( ( unsigned char * ) key, klen, ( unsigned char * ) &digest );

        int i = 0;

        for ( i = 0; i < 16; i++ ) {
            sprintf ( &md5chars[i * 2], "%02x", ( unsigned int ) digest[i] );
        }

        klen = 32 + sprintf ( md5chars + 32, "-%ld", fnv1a_32 ( key, klen ) );

        key = md5chars;
    }

    char *value = NULL;
    int flag = 0;

    if ( yac_storage_find ( key, klen, &value, &vlen, &flag, ( int * ) 0 ) ) {
        //if(value){
        int t = 1;

        if ( value[0] == '4' ) {
            t = 4;

        } else if ( value[0] == '3' ) {
            t = 3;

        } else if ( value[0] == '2' ) {
            t = 2;
        }

        lua_pushnumber ( L, t ); /// push type
        lua_pushlstring ( L, value + 1, vlen - 1 ); /// push content
        free ( value );
        return 2;

    } else {
        lua_pushnil ( L );
    }

    return 1;
}

int lua_f_cache_del ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    size_t klen = 0;
    const char *key = lua_tolstring ( L, 1, &klen );

    if ( klen < 1 ) {
        lua_pushnil ( L );
        return 1;
    }

    if ( klen > 48 ) {
        memset ( digest, 0, MD5_DIGEST_LENGTH );
        MD5 ( ( unsigned char * ) key, klen, ( unsigned char * ) &digest );

        int i = 0;

        for ( i = 0; i < 16; i++ ) {
            sprintf ( &md5chars[i * 2], "%02x", ( unsigned int ) digest[i] );
        }

        klen = 32 + sprintf ( md5chars + 32, "-%ld", fnv1a_32 ( key, klen ) );

        key = md5chars;
    }

    lua_pushboolean ( L, yac_storage_delete ( ( char * ) key, klen, 300 ) );

    return 1;
}
