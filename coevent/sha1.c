#include "coevent.h"

static char sha_buf[SHA_DIGEST_LENGTH];

int lua_f_sha1bin ( lua_State *L )
{
    const char *src = NULL;
    size_t slen = 0;

    if ( lua_isnil ( L, 1 ) ) {
        src = "";

    } else {
        src = luaL_checklstring ( L, 1, &slen );
    }

    SHA_CTX sha;
    SHA1_Init ( &sha );
    SHA1_Update ( &sha, src, slen );
    SHA1_Final ( sha_buf, &sha );
    lua_pushlstring ( L, ( char * ) sha_buf, sizeof ( sha_buf ) );
    return 1;
}
