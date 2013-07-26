#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "fastlz.h"

#define large_malloc(s) (malloc(((int)(s/4096)+1)*4096))
static char temp_buf[4096];

int lua_f_fastlz_compress ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        lua_pushnil ( L );
        return 1;
    }

    size_t vlen = 0;
    const char *value = lua_tolstring ( L, 1, &vlen );

    if ( vlen < 1 || vlen > 1 * 1024 * 1024 ) { /// Max 1Mb !!!
        lua_pushnil ( L );
        return 1;
    }

    char *dst = ( char * ) &temp_buf;

    if ( vlen + 20 > 4096 ) {
        dst = large_malloc ( vlen + 20 );
    }

    if ( dst == NULL ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "not enough memory!" );
        return 2;
    }

    const unsigned int size_header = htonl ( vlen );
    memcpy ( dst, &size_header, sizeof ( unsigned int ) );

    int dlen = fastlz_compress ( value, vlen, dst + sizeof ( unsigned int ) );

    if ( dst ) {
        lua_pushlstring ( L, dst, dlen + sizeof ( unsigned int ) );

        if ( dst != ( char * ) &temp_buf ) {
            free ( dst );
        }

        return 1;

    } else {
        lua_pushnil ( L );
        return 1;
    }
}

int lua_f_fastlz_decompress ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        lua_pushnil ( L );
        return 1;
    }

    size_t vlen = 0;
    const char *value = lua_tolstring ( L, 1, &vlen );

    if ( vlen < 1 ) {
        lua_pushnil ( L );
        return 1;
    }

    unsigned int value_len = 0;
    memcpy ( &value_len, value, sizeof ( unsigned int ) );
    value_len = ntohl ( value_len );

    if ( value_len > 1024 * 1024 + 20 ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "not enough memory!" );
        return 2;
    }

    char *dst = ( char * ) &temp_buf;

    if ( value_len + 20 > 4096 ) {
        dst = ( unsigned char * ) large_malloc ( value_len + 20 );
    }

    if ( dst == NULL ) {
        lua_pushnil ( L );
        lua_pushstring ( L, "not enough memory!" );
        return 2;
    }

    int dlen = fastlz_decompress ( value + sizeof ( unsigned int ),
                                   vlen - sizeof ( unsigned int ), dst, value_len + 20 );

    if ( dst ) {
        lua_pushlstring ( L, dst, value_len );

        if ( dst != ( char * ) &temp_buf ) {
            free ( dst );
        }

        return 1;

    } else {
        lua_pushnil ( L );
        return 1;
    }
}

LUALIB_API int luaopen_fastlz ( lua_State *L )
{
    lua_register ( L, "fastlz_compress", lua_f_fastlz_compress );
    lua_register ( L, "fastlz_decompress", lua_f_fastlz_decompress );
}
