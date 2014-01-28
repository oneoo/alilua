#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <iconv.h>
#include <errno.h>
#include <sys/types.h>

#define MYNAME "string-utils"

#include "lua.h"
#include "lauxlib.h"

#ifndef RELEASE
#  define RELEASE "0.1"
#endif

#define large_malloc(s) (malloc(((int)(s/4096)+1)*4096))

#define ERROR_NO_MEMORY     1
#define ERROR_INVALID       2
#define ERROR_INCOMPLETE    3
#define ERROR_UNKNOWN       4
#define ERROR_FINALIZED     5

typedef struct {
    iconv_t cd;
    uint32_t key;
    void *next;
} _iconv_t;

static _iconv_t *_cds[32];
static char temp_buf[4096];

#define ESCAPE_URI 0
#define ESCAPE_ARGS 1
#define ESCAPE_URI_COMPONENT 2
#define ESCAPE_HTML 3
#define ESCAPE_REFRESH 4
#define ESCAPE_MEMCACHED 5
#define ESCAPE_MAIL_AUTH 6
#define UNESCAPE_URI 1
#define UNESCAPE_REDIRECT 2
#define UNESCAPE_URI_COMPONENT 0

static uintptr_t ngx_http_lua_escape_uri ( u_char *dst, u_char *src, size_t size,
        unsigned int type )
{
    unsigned int n;
    uint32_t *escape;
    static u_char hex[] = "0123456789ABCDEF";
    /* " ", "#", "%", "?", %00-%1F, %7F-%FF */
    static uint32_t uri[] = {
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */

        /* ?>=< ;:98 7654 3210 /.-, +*)( '&%$ #"! */
        0xfc00886d, /* 1111 1100 0000 0000 1000 1000 0110 1101 */

        /* _^]\ [ZYX WVUT SRQP ONML KJIH GFED CBA@ */
        0x78000000, /* 0111 1000 0000 0000 0000 0000 0000 0000 */

        /* ~}| {zyx wvut srqp onml kjih gfed cba` */
        0xa8000000, /* 1010 1000 0000 0000 0000 0000 0000 0000 */

        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff /* 1111 1111 1111 1111 1111 1111 1111 1111 */
    };
    /* " ", "#", "%", "+", "?", %00-%1F, %7F-%FF */
    static uint32_t args[] = {
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */

        /* ?>=< ;:98 7654 3210 /.-, +*)( '&%$ #"! */
        0x80000829, /* 1000 0000 0000 0000 0000 1000 0010 1001 */

        /* _^]\ [ZYX WVUT SRQP ONML KJIH GFED CBA@ */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */

        /* ~}| {zyx wvut srqp onml kjih gfed cba` */
        0x80000000, /* 1000 0000 0000 0000 0000 0000 0000 0000 */

        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff /* 1111 1111 1111 1111 1111 1111 1111 1111 */
    };
    /* " ", "#", """, "%", "'", %00-%1F, %7F-%FF */
    static uint32_t html[] = {
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */

        /* ?>=< ;:98 7654 3210 /.-, +*)( '&%$ #"! */
        0x000000ad, /* 0000 0000 0000 0000 0000 0000 1010 1101 */

        /* _^]\ [ZYX WVUT SRQP ONML KJIH GFED CBA@ */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */

        /* ~}| {zyx wvut srqp onml kjih gfed cba` */
        0x80000000, /* 1000 0000 0000 0000 0000 0000 0000 0000 */

        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff /* 1111 1111 1111 1111 1111 1111 1111 1111 */
    };
    /* " ", """, "%", "'", %00-%1F, %7F-%FF */
    static uint32_t refresh[] = {
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */

        /* ?>=< ;:98 7654 3210 /.-, +*)( '&%$ #"! */
        0x00000085, /* 0000 0000 0000 0000 0000 0000 1000 0101 */

        /* _^]\ [ZYX WVUT SRQP ONML KJIH GFED CBA@ */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */

        /* ~}| {zyx wvut srqp onml kjih gfed cba` */
        0x80000000, /* 1000 0000 0000 0000 0000 0000 0000 0000 */

        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */
        0xffffffff /* 1111 1111 1111 1111 1111 1111 1111 1111 */
    };
    /* " ", "%", %00-%1F */
    static uint32_t memcached[] = {
        0xffffffff, /* 1111 1111 1111 1111 1111 1111 1111 1111 */

        /* ?>=< ;:98 7654 3210 /.-, +*)( '&%$ #"! */
        0x00000021, /* 0000 0000 0000 0000 0000 0000 0010 0001 */

        /* _^]\ [ZYX WVUT SRQP ONML KJIH GFED CBA@ */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */

        /* ~}| {zyx wvut srqp onml kjih gfed cba` */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */

        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */
        0x00000000, /* 0000 0000 0000 0000 0000 0000 0000 0000 */
    };
    /* mail_auth is the same as memcached */
    static uint32_t *map[] =
    { uri, args, html, refresh, memcached, memcached };
    escape = map[type];

    if ( dst == NULL ) {
        /* find the number of the characters to be escaped */
        n = 0;

        while ( size ) {
            if ( escape[*src >> 5] & ( 1 << ( *src & 0x1f ) ) ) {
                n++;
            }

            src++;
            size--;
        }

        return ( uintptr_t ) n;
    }

    while ( size ) {
        if ( escape[*src >> 5] & ( 1 << ( *src & 0x1f ) ) ) {
            *dst++ = '%';
            *dst++ = hex[*src >> 4];
            *dst++ = hex[*src & 0xf];
            src++;

        } else {
            *dst++ = *src++;
        }

        size--;
    }

    return ( uintptr_t ) dst;
}

/* XXX we also decode '+' to ' ' */
static void ngx_http_lua_unescape_uri ( u_char **dst, u_char **src, size_t size,
                                        unsigned int type )
{
    u_char *d, *s, ch, c, decoded;
    enum {
        sw_usual = 0,
        sw_quoted,
        sw_quoted_second
    } state;
    d = *dst;
    s = *src;
    state = 0;
    decoded = 0;

    while ( size-- ) {
        ch = *s++;

        switch ( state ) {
            case sw_usual:
                if ( ch == '?'
                     && ( type & ( UNESCAPE_URI | UNESCAPE_REDIRECT ) ) ) {
                    *d++ = ch;
                    goto done;
                }

                if ( ch == '%' ) {
                    state = sw_quoted;
                    break;
                }

                if ( ch == '+' ) {
                    *d++ = ' ';
                    break;
                }

                *d++ = ch;
                break;

            case sw_quoted:
                if ( ch >= '0' && ch <= '9' ) {
                    decoded = ( u_char ) ( ch - '0' );
                    state = sw_quoted_second;
                    break;
                }

                c = ( u_char ) ( ch | 0x20 );

                if ( c >= 'a' && c <= 'f' ) {
                    decoded = ( u_char ) ( c - 'a' + 10 );
                    state = sw_quoted_second;
                    break;
                }

                /* the invalid quoted character */
                state = sw_usual;
                *d++ = ch;
                break;

            case sw_quoted_second:
                state = sw_usual;

                if ( ch >= '0' && ch <= '9' ) {
                    ch = ( u_char ) ( ( decoded << 4 ) + ch - '0' );

                    if ( type & UNESCAPE_REDIRECT ) {
                        if ( ch > '%' && ch < 0x7f ) {
                            *d++ = ch;
                            break;
                        }

                        *d++ = '%';
                        *d++ = * ( s - 2 );
                        *d++ = * ( s - 1 );
                        break;
                    }

                    *d++ = ch;
                    break;
                }

                c = ( u_char ) ( ch | 0x20 );

                if ( c >= 'a' && c <= 'f' ) {
                    ch = ( u_char ) ( ( decoded << 4 ) + c - 'a' + 10 );

                    if ( type & UNESCAPE_URI ) {
                        if ( ch == '?' ) {
                            *d++ = ch;
                            goto done;
                        }

                        *d++ = ch;
                        break;
                    }

                    if ( type & UNESCAPE_REDIRECT ) {
                        if ( ch == '?' ) {
                            *d++ = ch;
                            goto done;
                        }

                        if ( ch > '%' && ch < 0x7f ) {
                            *d++ = ch;
                            break;
                        }

                        *d++ = '%';
                        *d++ = * ( s - 2 );
                        *d++ = * ( s - 1 );
                        break;
                    }

                    *d++ = ch;
                    break;
                }

                /* the invalid quoted character */
                break;
        }
    }

done:
    *dst = d;
    *src = s;
}

static int lua_f_escape_uri ( lua_State *L )
{
    size_t len, dlen;
    uintptr_t escape;
    u_char *src, *dst;

    if ( lua_gettop ( L ) < 1 ) {
        return luaL_error ( L, "expecting one argument" );
    }

    src = ( u_char * ) luaL_checklstring ( L, 1, &len );

    if ( len == 0 ) {
        return 1;
    }

    escape = 2 * ngx_http_lua_escape_uri ( NULL, src, len, ESCAPE_URI );

    if ( escape ) {
        dlen = escape + len;
        dst = lua_newuserdata ( L, dlen );
        ngx_http_lua_escape_uri ( dst, src, len, ESCAPE_URI );
        lua_pushlstring ( L, ( char * ) dst, dlen );
    }

    return 1;
}

static int lua_f_unescape_uri ( lua_State *L )
{
    size_t len, dlen;
    u_char *p;
    u_char *src, *dst;

    if ( lua_gettop ( L ) != 1 ) {
        return luaL_error ( L, "expecting one argument" );
    }

    src = ( u_char * ) luaL_checklstring ( L, 1, &len );
    /* the unescaped string can only be smaller */
    dlen = len;
    p = lua_newuserdata ( L, dlen );
    dst = p;
    ngx_http_lua_unescape_uri ( &dst, &src, len, UNESCAPE_URI_COMPONENT );
    lua_pushlstring ( L, ( char * ) p, dst - p );
    return 1;
}

#define base64_encoded_length(len) (((len + 2) / 3) * 4)
#define base64_decoded_length(len) (((len + 3) / 4) * 3)
static char basis64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int base64encode ( unsigned char *dst, const unsigned char *src, int len )
{
    unsigned char *d;
    const unsigned char *s;
    s = src;
    d = dst;

    while ( len > 2 ) {
        *d++ = basis64[ ( s[0] >> 2 ) & 0x3f];
        *d++ = basis64[ ( ( s[0] & 3 ) << 4 ) | ( s[1] >> 4 )];
        *d++ = basis64[ ( ( s[1] & 0x0f ) << 2 ) | ( s[2] >> 6 )];
        *d++ = basis64[s[2] & 0x3f];
        s += 3;
        len -= 3;
    }

    if ( len ) {
        *d++ = basis64[ ( s[0] >> 2 ) & 0x3f];

        if ( len == 1 ) {
            *d++ = basis64[ ( s[0] & 3 ) << 4];
            *d++ = '=';

        } else {
            *d++ = basis64[ ( ( s[0] & 3 ) << 4 ) | ( s[1] >> 4 )];
            *d++ = basis64[ ( s[1] & 0x0f ) << 2];
        }

        *d++ = '=';
    }

    return d - dst;
}

static int base64_decode_internal ( unsigned char *dst, const unsigned char *src,
                                    size_t slen, const unsigned char *basis )
{
    size_t len;
    unsigned char *d;
    const unsigned char *s;

    for ( len = 0; len < slen; len++ ) {
        if ( src[len] == '=' ) {
            break;
        }

        if ( basis[src[len]] == 77 ) {
            return 0;
        }
    }

    if ( len % 4 == 1 ) {
        return 0;
    }

    s = src;
    d = dst;

    while ( len > 3 ) {
        *d++ = ( char ) ( basis[s[0]] << 2 | basis[s[1]] >> 4 );
        *d++ = ( char ) ( basis[s[1]] << 4 | basis[s[2]] >> 2 );
        *d++ = ( char ) ( basis[s[2]] << 6 | basis[s[3]] );
        s += 4;
        len -= 4;
    }

    if ( len > 1 ) {
        *d++ = ( char ) ( basis[s[0]] << 2 | basis[s[1]] >> 4 );
    }

    if ( len > 2 ) {
        *d++ = ( char ) ( basis[s[1]] << 4 | basis[s[2]] >> 2 );
    }

    return d - dst;
}

static int base64decode ( unsigned char *dst, const unsigned char *src, size_t slen )
{
    static unsigned char basis64[] = {
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
        77, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
        77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
    };
    return base64_decode_internal ( dst, src, slen, basis64 );
}

static int base64decode_url ( unsigned char *dst, const unsigned char *src,
                              size_t slen )
{
    static char basis64[] = {
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
        77, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
        77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
    };
    return base64_decode_internal ( dst, src, slen, basis64 );
}

static int lua_f_base64_encode ( lua_State *L )
{
    const char *src = NULL;
    size_t slen = 0;

    if ( lua_isnil ( L, 1 ) ) {
        src = "";

    } else {
        src = luaL_checklstring ( L, 1, &slen );
    }

    char *end = lua_newuserdata ( L,
                                  base64_encoded_length ( slen ) ); //large_malloc(base64_encoded_length(slen));
    int nlen = base64encode ( end, src, slen );
    lua_pushlstring ( L, end, nlen );
    //free(end);
    return 1;
}

static int lua_f_base64_decode ( lua_State *L )
{
    const char *src = NULL;
    size_t slen = 0;

    if ( lua_isnil ( L, 1 ) ) {
        src = "";

    } else {
        src = luaL_checklstring ( L, 1, &slen );
    }

    char *end = lua_newuserdata ( L,
                                  base64_decoded_length ( slen ) ); //large_malloc(base64_decoded_length(slen));
    int nlen = base64decode ( end, src, slen );
    lua_pushlstring ( L, end, nlen );
    //free(end);
    return 1;
}

//Characters encoded are NUL (ASCII 0), \n, \r, \, ', ", and Control-Z.
static int lua_f_escape ( lua_State *L )
{
    const char *src = NULL;
    size_t slen = 0;

    if ( lua_isnil ( L, 1 ) ) {
        src = "";

    } else {
        src = luaL_checklstring ( L, 1, &slen );
    }

    if ( src == 0 ) {
        lua_pushstring ( L, "" );
        return 1;
    }

    char *dst = temp_buf;
    int i = 0, j = 0, has = 0;

    for ( i = 0; i < slen; i++ ) {
        if ( j >= 4 ) {
            lua_pushlstring ( L, temp_buf, j );

            if ( has == 1 ) {
                lua_concat ( L, 2 );
            }

            has = 1;
            j = 0;
        }

        switch ( src[i] ) {
            case '\r':
                temp_buf[j++] = '\\';
                temp_buf[j++] = 'r';
                continue;
                break;

            case '\n':
                temp_buf[j++] = '\\';
                temp_buf[j++] = 'n';
                continue;
                break;

            case '\\':
                temp_buf[j++] = '\\';
                break;

            case '\'':
                temp_buf[j++] = '\\';
                break;

            case '"':
                temp_buf[j++] = '\\';
                break;

            case '\b':
                temp_buf[j++] = '\\';
                temp_buf[j++] = 'b';
                continue;
                break;

            case '\t':
                temp_buf[j++] = '\\';
                temp_buf[j++] = 't';
                continue;
                break;

            case '\0':
                temp_buf[j++] = '\\';
                temp_buf[j++] = '0';
                continue;
                break;

            case '\032':
                temp_buf[j++] = '\\';
                temp_buf[j++] = 'Z';
                continue;
                break;

            default:
                break;
        }

        temp_buf[j++] = src[i];
    }

    lua_pushlstring ( L, temp_buf, j );

    if ( has == 1 ) {
        lua_concat ( L, 2 );
    }

    return 1;
}

static char *stristr ( const char *str, const char *pat, int length )
{
    if ( !str || !pat ) {
        return ( NULL );
    }

    if ( length < 1 ) {
        length = strlen ( str );
    }

    int pat_len = strlen ( pat );

    if ( length < pat_len ) {
        return NULL;
    }

    int i = 0;

    for ( i = 0; i < length; i++ ) {
        if ( toupper ( str[i] ) == toupper ( pat[0] ) && ( length - i ) >= pat_len
             && toupper ( str[i + pat_len - 1] ) == toupper ( pat[pat_len - 1] ) ) {
            int j = i;

            for ( ; j < i + pat_len; j++ ) {
                if ( toupper ( str[j] ) != toupper ( pat[j - i] ) ) {
                    break;
                }
            }

            if ( j < i + pat_len ) {
                continue;
            }

            return ( char * ) str + i;
        }
    }

    return ( NULL );
}

static int lua_f_startsWith ( lua_State *L )
{
    int nargs = lua_gettop ( L );

    if ( nargs < 2 || !lua_isstring ( L, 1 ) || !lua_isstring ( L, 2 ) ) {
        return luaL_error ( L, "expecting two string argument" );
    }

    size_t len = 0;
    size_t len2 = 0;
    const char *p = lua_tolstring ( L, 1, &len );
    const char *n = lua_tolstring ( L, 2, &len2 );

    if ( len2 > len ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    if ( nargs == 3 && lua_isboolean ( L, 3 ) && lua_toboolean ( L, 3 ) ) {
        if ( stristr ( p, n, len ) != p ) {
            lua_pushboolean ( L, 0 );

        } else {
            lua_pushboolean ( L, 1 );
        }

    } else {
        if ( strstr ( p, n ) != p ) {
            lua_pushboolean ( L, 0 );

        } else {
            lua_pushboolean ( L, 1 );
        }
    }

    return 1;
}

static int lua_f_endsWith ( lua_State *L )
{
    int nargs = lua_gettop ( L );

    if ( nargs < 2 || !lua_isstring ( L, 1 ) || !lua_isstring ( L, 2 ) ) {
        return luaL_error ( L, "expecting two string argument" );
    }

    size_t len = 0;
    size_t len2 = 0;
    const char *p = lua_tolstring ( L, 1, &len );
    const char *n = lua_tolstring ( L, 2, &len2 );

    if ( len2 > len ) {
        lua_pushboolean ( L, 0 );
        return 1;
    }

    p = p + ( len - len2 );
    len = len2;

    if ( nargs == 3 && lua_isboolean ( L, 3 ) && lua_toboolean ( L, 3 ) ) {
        if ( stristr ( p, n, len ) != p ) {
            lua_pushboolean ( L, 0 );

        } else {
            lua_pushboolean ( L, 1 );
        }

    } else {
        if ( strstr ( p, n ) != p ) {
            lua_pushboolean ( L, 0 );

        } else {
            lua_pushboolean ( L, 1 );
        }
    }

    return 1;
}

int lua_f_explode ( lua_State *L )
{
    if ( lua_gettop ( L ) != 2 || !lua_isstring ( L, 1 ) || !lua_isstring ( L, 2 ) ) {
        return luaL_error ( L, "expecting two string argument" );
    }

    lua_newtable ( L );
    int i = 1;
    const char *tok = NULL;
    const char *pat = lua_tostring ( L, 2 );
    tok = strtok ( ( char * ) lua_tostring ( L, 1 ), pat );

    while ( tok ) {
        lua_pushstring ( L, tok );
        lua_rawseti ( L, -2, i++ );
        tok = strtok ( NULL, pat );
    }

    return 1;
}

#define C_LT        '<'
#define C_GT        '>'
#define C_QUOT      '"'
#define C_APOS      '\''
#define C_BSLASH    '\\'
static int lua_f_strip ( lua_State *L )
{
    if ( lua_type ( L, 1 ) == LUA_TSTRING ) {
        size_t len = 0;
        const char *str = luaL_checklstring ( L, 1, &len );
        char chaser[len];
        char quot = 0;
        char needle = 0;
        size_t tail = 0;
        size_t head = 0;

        for ( ; tail < len; tail++ ) {
            switch ( str[tail] ) {
                case C_LT:
                    if ( !quot ) {
                        needle = str[tail];
                    }

                    break;

                case C_GT:
                    if ( needle ) {
                        needle = 0;
                        continue;
                    }

                    break;

                case C_QUOT:
                case C_APOS:
                    if ( !needle && tail && str[tail - 1] != C_BSLASH ) {
                        if ( !quot ) {
                            quot = str[tail];

                        } else if ( quot == str[tail] ) {
                            quot = 0;
                        }
                    }

                    break;
            }

            if ( !needle ) {
                chaser[head++] = str[tail];
            }
        }

        chaser[head] = 0;
        lua_pushlstring ( L, chaser, head );
        return 1;
    }

    return 0;
}

static uint32_t fnv1a_64 ( const char *data, uint32_t len )
{
    uint64_t rv = 0xcbf29ce484222325UL;
    uint32_t i;

    for ( i = 0; i < len; i++ ) {
        rv = ( rv ^ ( unsigned char ) data[i] ) * 1099511628211UL;
    }

    return ( uint32_t ) rv;
}

static iconv_t get_iconv ( const char *form, const char *to )
{
    int len = sprintf ( temp_buf, "%s%s", form, to );
    int key = fnv1a_64 ( temp_buf, len ) % 32;
    _iconv_t *n = _cds[key], *un = NULL;

    while ( n ) {
        if ( n->key == key ) {
            break;
        }

        un = n;
        n = ( _iconv_t * ) n->next;
    }

    if ( !n ) {
        n = malloc ( sizeof ( _iconv_t ) );
        n->key = key;
        n->cd = iconv_open ( to, form );
        n->next = NULL;

        if ( !_cds[key] ) {
            _cds[key] = n;

        } else {
            un->next = n;
        }
    }

    return n->cd;
}

static int lua_f_iconv ( lua_State *L )
{
    if ( lua_gettop ( L ) < 2 || !lua_isstring ( L, 1 ) || !lua_isstring ( L, 2 ) ) {
        return luaL_error ( L, "expecting arguments" );
    }

    size_t src_len = 0;
    char *src = ( char * ) lua_tolstring ( L, 1, &src_len );
    const char *form = lua_tostring ( L, 2 );
    const char *to = "utf-8";

    if ( lua_isstring ( L, 3 ) ) {
        to = lua_tostring ( L, 3 );
    }

    iconv_t *cd = get_iconv ( form, to );

    if ( !cd ) {
        lua_pushnumber ( L, 0 );
        lua_pushstring ( L, "cannot open iconv" );
        return 2;
    }

    size_t obsize = 4096;
    size_t obleft = obsize;
    size_t ret = -1;
    char *outbuf = temp_buf;
    int hasone = 0;
    char *outbufs = outbuf;

    do {
        ret = iconv ( cd, &src, &src_len, &outbuf, &obleft );

        if ( ret == ( size_t ) ( -1 ) ) {
            lua_pushlstring ( L, outbufs, obsize - obleft );

            if ( hasone == 1 ) {
                lua_concat ( L, 2 );
            }

            hasone = 1;

            if ( errno == EILSEQ ) {
                lua_pushnumber ( L, ERROR_INVALID );
                return 2;   /* Invalid character sequence */

            } else if ( errno == EINVAL ) {
                lua_pushnumber ( L, ERROR_INCOMPLETE );
                return 2;   /* Incomplete character sequence */

            } else if ( errno == E2BIG ) {
                obleft = obsize;
                outbuf = outbufs;

            } else {
                lua_pushnumber ( L, ERROR_UNKNOWN );
                return 2; /* Unknown error */
            }
        }
    } while ( ret == ( size_t ) - 1 );

    lua_pushlstring ( L, outbufs, obsize - obleft );

    if ( hasone == 1 ) {
        lua_concat ( L, 2 );
    }

    lua_pushnil ( L );
    return 2;
}

static int _php_iconv_strlen ( unsigned int *pretval, const char *str, size_t nbytes,
                               const char *enc )
{
    char buf[4 * 2];
    int err = 0;

    const char *in_p;
    size_t in_left;

    char *out_p;
    size_t out_left;

    unsigned int cnt = 0;

    *pretval = ( unsigned int ) - 1;

    iconv_t *cd = get_iconv ( enc, "UCS-4" );

    if ( cd == ( iconv_t ) ( -1 ) ) {
        return -1;
    }

    errno = out_left = 0;

    for ( in_p = str, in_left = nbytes, cnt = 0; in_left > 0; cnt += 2 ) {
        size_t prev_in_left;
        out_p = buf;
        out_left = sizeof ( buf );

        prev_in_left = in_left;

        if ( iconv ( cd, ( char ** ) &in_p, &in_left, ( char ** ) &out_p,
                     &out_left ) == ( size_t ) - 1 ) {
            if ( prev_in_left == in_left ) {
                break;
            }
        }
    }

    if ( out_left > 0 ) {
        cnt -= out_left / 4;
    }

    *pretval = cnt;

    return err;
}

static int lua_f_iconv_strlen ( lua_State *L )
{
    if ( !lua_isstring ( L, 1 ) ) {
        return luaL_error ( L, "expecting string argument" );
    }

    size_t src_len = 0;
    const char *src = lua_tolstring ( L, 1, &src_len );
    const char *enc = "utf-8";

    if ( lua_isstring ( L, 2 ) ) {
        enc = lua_tostring ( L, 2 );
    }

    int _len = 0;

    if ( _php_iconv_strlen ( &_len, src, src_len, enc ) == 0 ) {
        lua_pushnumber ( L, _len );

    } else {
        lua_pushnumber ( L, 0 );
    }

    return 1;
}

static int lua_f_iconv_substr ( lua_State *L )
{
    if ( lua_gettop ( L ) < 2 || !lua_isstring ( L, 1 ) ) {
        return luaL_error ( L, "expecting arguments" );
    }

    size_t src_len = 0;
    const char *src = lua_tolstring ( L, 1, &src_len );
    const char *enc = "utf-8";

    int sub_start = 0;
    int sub_end = src_len;
    int ps = 2;

    if ( lua_isnumber ( L, ps ) ) {
        sub_start = lua_tonumber ( L, ps );

        if ( sub_start < 0 ) {
            sub_start = 0;
        }

        ps ++;
    }

    if ( lua_isnumber ( L, ps ) ) {
        sub_end = lua_tonumber ( L, ps );

        if ( sub_end >= 0 ) {
            sub_end += sub_start;
        }

        ps ++;
    }

    if ( lua_isstring ( L, ps ) ) {
        enc = lua_tostring ( L, ps );
    }

    if ( sub_end < 0 ) {
        int _len = 0;

        if ( _php_iconv_strlen ( &_len, src, src_len, enc ) == 0 ) {
            sub_end += _len - 1;

        } else {
            lua_pushnumber ( L, 0 );
            lua_pushstring ( L, "cannot convert string" );
            return 2;
        }
    }

    char *obuf = temp_buf;
    int olen = 0;
    int pushed = 0;

    char buf[4];
    const char *in_p;
    size_t in_left;

    char *out_p;
    size_t out_left;

    unsigned int cnt = 0;


    iconv_t *cd = get_iconv ( enc, "UCS-4" );

    if ( cd == ( iconv_t ) ( -1 ) ) {
        lua_pushnumber ( L, 0 );
        lua_pushstring ( L, "cannot open iconv" );
        return 2;
    }

    errno = out_left = 0;

    for ( in_p = src, in_left = src_len, cnt = 0; in_left > 0; cnt ++ ) {
        size_t prev_in_left;
        out_p = buf;
        out_left = sizeof ( buf );

        prev_in_left = in_left;

        if ( iconv ( cd, ( char ** ) &in_p, &in_left, ( char ** ) &out_p,
                     &out_left ) == ( size_t ) - 1 ) {
            if ( prev_in_left == in_left ) {
                break;
            }
        }

        if ( cnt - out_left / 4 >= sub_start ) {
            if ( olen >= sizeof ( temp_buf ) * 0.6 ) {
                lua_pushlstring ( L, obuf, olen );
                olen = 0;

                if ( pushed == 1 ) {
                    lua_concat ( L, 2 );
                }

                pushed = 1;
            }

            memcpy ( obuf + olen, src + ( src_len - prev_in_left ), prev_in_left - in_left );
            olen += prev_in_left - in_left;
        }

        if ( cnt - out_left / 4 >= sub_end ) {
            break;
        }
    }

    lua_pushlstring ( L, obuf, olen );

    if ( pushed == 1 ) {
        lua_concat ( L, 2 );
    }

    return 1;
}

static int lua_f_nl2br ( lua_State *L )
{
    if ( lua_type ( L, 1 ) == LUA_TSTRING ) {
        size_t slen = 0;
        int hasone = 0;
        int i = 0, k = 0;
        const char *src = lua_tolstring ( L, 1, &slen );

        for ( i = 0; i < slen; i++ ) {
            if ( k > 4090 ) {
                lua_pushlstring ( L, temp_buf, k );

                if ( hasone == 1 ) {
                    lua_concat ( L, 2 );
                }

                hasone = 1;
                k = 0;
            }

            if ( src[i] == '\n' ) {
                if ( temp_buf[k - 1] == '\r' ) {
                    temp_buf[k - 1] = '<';

                } else {
                    temp_buf[k++] = '<';
                }

                temp_buf[k++] = 'b';
                temp_buf[k++] = 'r';
                temp_buf[k++] = '/';
                temp_buf[k++] = '>';

            } else {
                temp_buf[k++] = src[i];
            }
        }

        lua_pushlstring ( L, temp_buf, k );

        if ( hasone == 1 ) {
            lua_concat ( L, 2 );
        }

        return 1;
    }

    return 0;
}

static const luaL_reg F[] = {
    {"escape",              lua_f_escape},
    {"escape_uri",          lua_f_escape_uri},
    {"unescape_uri",        lua_f_unescape_uri},
    {"base64_encode",       lua_f_base64_encode},
    {"base64_decode",       lua_f_base64_decode},

    {"startsWith",          lua_f_startsWith},
    {"endsWith",            lua_f_endsWith},

    {"explode",             lua_f_explode},
    {"strip",               lua_f_strip},
    {"nl2br",               lua_f_nl2br},

    {"iconv",               lua_f_iconv},
    {"iconv_strlen",        lua_f_iconv_strlen},
    {"iconv_substr",        lua_f_iconv_substr},

    {NULL,                  NULL}
};

LUALIB_API int luaopen_string_utils ( lua_State *L )
{
    int i = 0;

    for ( i = 0; i < 32; i++ ) {
        _cds[i] = NULL;
    }

    /* Version number from CVS marker. */
#ifdef PRE_LUA51
    luaL_openlib ( L, MYNAME, F, 0 );
#else
    luaL_register ( L, MYNAME, F );
#endif
    return 1;
}