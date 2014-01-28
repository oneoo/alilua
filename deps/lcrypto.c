/*
** $Id: lcrypto.c,v 1.3 2006-09-04 20:32:57 nezroy Exp $
** See Copyright Notice in license.html
*/

#include <string.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#define CRYPTO_OPENSSL 1

#include "lua.h"
#include "lauxlib.h"
#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif

#include "lcrypto.h"

#if CRYPTO_OPENSSL
#define LUACRYPTO_ENGINE "OpenSSL"
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#define HANDLER_EVP EVP_MD_CTX
#define HANDLER_HMAC HMAC_CTX
#define DIGEST_TYPE const EVP_MD*
#define DIGEST_BY_NAME(s) EVP_get_digestbyname(s)
#define IS_DIGEST_INVALID(x) (x==NULL)
#define EVP_UPDATE(c,s,len) EVP_DigestUpdate(c, s, len)
#define HMAC_UPDATE(c,s,len) HMAC_Update(c, (unsigned char *)s, len);
#define EVP_CLEANUP(c) EVP_MD_CTX_cleanup(c);
#define HMAC_CLEANUP(c) HMAC_CTX_cleanup(c);
#elif CRYPTO_GCRYPT
#define LUACRYPTO_ENGINE "gcrypt"
#include <gcrypt.h>
#define HANDLER_EVP gcry_md_hd_t
#define HANDLER_HMAC gcry_md_hd_t
#define DIGEST_TYPE int
#define DIGEST_BY_NAME(s) gcry_md_map_name(s)
#define IS_DIGEST_INVALID(x) (x==0)
#define EVP_UPDATE(c,s,len) gcry_md_write(*c, s, len)
#define HMAC_UPDATE(c,s,len) gcry_md_write(*c, s, len)
#define EVP_CLEANUP(c) gcry_md_close(*c)
#define HMAC_CLEANUP(c) gcry_md_close(*c)
#else
#error "LUACRYPTO_DRIVER not supported"
#endif

LUACRYPTO_API int luaopen_crypto ( lua_State *L );

static char *bin2hex ( const unsigned char *digest, size_t written )
{
    char *hex = calloc ( sizeof ( char ), written * 2 + 1 );
    unsigned int i;

    for ( i = 0; i < written; i++ ) {
        sprintf ( hex + 2 * i, "%02x", digest[i] );
    }

    return hex;
}

#if CRYPTO_OPENSSL
static int crypto_error ( lua_State *L )
{
    char buf[120];
    unsigned long e = ERR_get_error();
    ERR_load_crypto_strings();
    lua_pushnil ( L );
    lua_pushstring ( L, ERR_error_string ( e, buf ) );
    return 2;
}
#endif

static HANDLER_EVP *evp_pget ( lua_State *L, int i )
{
    if ( luaL_checkudata ( L, i, LUACRYPTO_EVPNAME ) == NULL ) {
        luaL_typerror ( L, i, LUACRYPTO_EVPNAME );
    }

    return lua_touserdata ( L, i );
}

static HANDLER_EVP *evp_pnew ( lua_State *L )
{
    HANDLER_EVP *c = lua_newuserdata ( L, sizeof ( HANDLER_EVP ) );
    luaL_getmetatable ( L, LUACRYPTO_EVPNAME );
    lua_setmetatable ( L, -2 );
    return c;
}

static int evp_fnew ( lua_State *L )
{
    HANDLER_EVP *c = NULL;
    const char *s = luaL_checkstring ( L, 1 );
    DIGEST_TYPE type = DIGEST_BY_NAME ( s );

    if ( IS_DIGEST_INVALID ( type ) ) {
        luaL_argerror ( L, 1, "invalid digest type" );
        return 0;
    }

    c = evp_pnew ( L );
#if CRYPTO_OPENSSL
    EVP_MD_CTX_init ( c );
    EVP_DigestInit_ex ( c, type, NULL ); //must return 1 (not checked!)
#elif CRYPTO_GCRYPT
    gcry_md_open ( c, type, 0 ); //returns a gcry_error_t (not checked!)
#endif
    return 1;
}

static int evp_clone ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
    HANDLER_EVP *d = evp_pnew ( L );
#if CRYPTO_OPENSSL
    EVP_MD_CTX_init ( d );
    EVP_MD_CTX_copy_ex ( d, c );
#elif CRYPTO_GCRYPT
    gcry_md_copy ( d, *c );
#endif
    return 1;
}

static int evp_reset ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
#if CRYPTO_OPENSSL
    const EVP_MD *t = EVP_MD_CTX_md ( c );
    EVP_MD_CTX_cleanup ( c );
    EVP_MD_CTX_init ( c );
    EVP_DigestInit_ex ( c, t, NULL );
#elif CRYPTO_GCRYPT
    gcry_md_reset ( *c );
#endif
    return 0;
}

static int evp_update ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
    size_t s_len;
    const char *s = luaL_checklstring ( L, 2, &s_len );
    EVP_UPDATE ( c, s, s_len );
    lua_settop ( L, 1 );
    return 1;
}

static int evp_digest ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
#if CRYPTO_OPENSSL
    HANDLER_EVP *d = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];
#elif CRYPTO_GCRYPT
    HANDLER_EVP d = NULL;
    unsigned char *digest;
    int algo;
#endif
    size_t written = 0;

    if ( lua_isstring ( L, 2 ) ) {
        size_t s_len;
        const char *s = luaL_checklstring ( L, 2, &s_len );
        EVP_UPDATE ( c, s, s_len );
    }

#if CRYPTO_OPENSSL
    d = EVP_MD_CTX_create();
    EVP_MD_CTX_copy_ex ( d, c );
    EVP_DigestFinal_ex ( d, digest, ( unsigned int * ) &written );
    EVP_MD_CTX_destroy ( d );
#elif CRYPTO_GCRYPT
    algo = gcry_md_get_algo ( *c );
    gcry_md_copy ( &d, *c );
    gcry_md_final ( d );
    digest = gcry_md_read ( d, algo );
    written = gcry_md_get_algo_dlen ( algo );
#endif

    if ( lua_toboolean ( L, 3 ) ) {
        lua_pushlstring ( L, ( char * ) digest, written );

    } else {
        char *hex = bin2hex ( digest, written );
        lua_pushlstring ( L, hex, written * 2 );
        free ( hex );
    }

#if CRYPTO_GCRYPT
    gcry_md_close ( d );
#endif
    return 1;
}

static int evp_tostring ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
    char s[64];
    sprintf ( s, "%s %p", LUACRYPTO_EVPNAME, ( void * ) c );
    lua_pushstring ( L, s );
    return 1;
}

static int evp_gc ( lua_State *L )
{
    HANDLER_EVP *c = evp_pget ( L, 1 );
    EVP_CLEANUP ( c );
    return 1;
}

static int evp_fdigest ( lua_State *L )
{
    const char *type_name = luaL_checkstring ( L, 1 );
    const char *s = luaL_checkstring ( L, 2 );
    DIGEST_TYPE type = DIGEST_BY_NAME ( type_name );
    size_t written = 0;
#if CRYPTO_OPENSSL
    HANDLER_EVP *c = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];
#elif CRYPTO_GCRYPT
    unsigned char digest[gcry_md_get_algo_dlen ( type )];
#endif

    if ( IS_DIGEST_INVALID ( type ) ) {
        luaL_argerror ( L, 1, "invalid digest type" );
        return 0;
    }

#if CRYPTO_OPENSSL
    c = EVP_MD_CTX_create();
    EVP_DigestInit_ex ( c, type, NULL );
    EVP_DigestUpdate ( c, s, lua_strlen ( L, 2 ) );
    EVP_DigestFinal_ex ( c, digest, ( unsigned int * ) &written );
#elif CRYPTO_GCRYPT
    gcry_md_hash_buffer ( type, digest, s, lua_strlen ( L, 2 ) );
    written = gcry_md_get_algo_dlen ( type );
#endif

    if ( lua_toboolean ( L, 3 ) ) {
        lua_pushlstring ( L, ( char * ) digest, written );

    } else {
        char *hex = bin2hex ( digest, written );
        lua_pushlstring ( L, hex, written * 2 );
        free ( hex );
    }

    return 1;
}

static HANDLER_HMAC *hmac_pget ( lua_State *L, int i )
{
    if ( luaL_checkudata ( L, i, LUACRYPTO_HMACNAME ) == NULL ) {
        luaL_typerror ( L, i, LUACRYPTO_HMACNAME );
    }

    return lua_touserdata ( L, i );
}

static HANDLER_HMAC *hmac_pnew ( lua_State *L )
{
    HANDLER_HMAC *c = lua_newuserdata ( L, sizeof ( HANDLER_HMAC ) );
    luaL_getmetatable ( L, LUACRYPTO_HMACNAME );
    lua_setmetatable ( L, -2 );
    return c;
}

static int hmac_fnew ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pnew ( L );
    const char *s = luaL_checkstring ( L, 1 );
    size_t k_len;
    const char *k = luaL_checklstring ( L, 2, &k_len );
    DIGEST_TYPE type = DIGEST_BY_NAME ( s );

    if ( IS_DIGEST_INVALID ( type ) ) {
        luaL_argerror ( L, 1, "invalid digest type" );
        return 0;
    }

#if CRYPTO_OPENSSL
    HMAC_CTX_init ( c );
    HMAC_Init_ex ( c, k, k_len, type, NULL );
#elif CRYPTO_GCRYPT
    gcry_md_open ( c, type, GCRY_MD_FLAG_HMAC );
    gcry_md_setkey ( *c, k, k_len );
#endif
    return 1;
}

static int hmac_clone ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
    HANDLER_HMAC *d = hmac_pnew ( L );
#if CRYPTO_OPENSSL
    *d = *c;
#elif CRYPTO_GCRYPT
    gcry_md_copy ( d, *c );
#endif
    return 1;
}

static int hmac_reset ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
#if CRYPTO_OPENSSL
    HMAC_Init_ex ( c, NULL, 0, NULL, NULL );
#elif CRYPTO_GCRYPT
    gcry_md_reset ( *c );
#endif
    return 0;
}

static int hmac_update ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
    size_t s_len;
    const char *s = luaL_checklstring ( L, 2, &s_len );
    HMAC_UPDATE ( c, s, s_len );
    lua_settop ( L, 1 );
    return 1;
}

static int hmac_digest ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
    size_t written = 0;
#if CRYPTO_OPENSSL
    unsigned char digest[EVP_MAX_MD_SIZE];
#elif CRYPTO_GCRYPT
    HANDLER_HMAC d;
    unsigned char *digest;
    int algo;
#endif

    if ( lua_isstring ( L, 2 ) ) {
        size_t s_len;
        const char *s = luaL_checklstring ( L, 2, &s_len );
        HMAC_UPDATE ( c, s, s_len );
    }

#if CRYPTO_OPENSSL
    HMAC_Final ( c, digest, ( unsigned int * ) &written );
#elif CRYPTO_GCRYPT
    algo = gcry_md_get_algo ( *c );
    gcry_md_copy ( &d, *c );
    gcry_md_final ( d );
    digest = gcry_md_read ( d, algo );
    written = gcry_md_get_algo_dlen ( algo );
#endif

    if ( lua_toboolean ( L, 3 ) ) {
        lua_pushlstring ( L, ( char * ) digest, written );

    } else {
        char *hex = bin2hex ( digest, written );
        lua_pushlstring ( L, hex, written * 2 );
        free ( hex );
    }

#if CRYPTO_GCRYPT
    gcry_md_close ( d );
#endif
    return 1;
}

static int hmac_tostring ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
    char s[64];
    sprintf ( s, "%s %p", LUACRYPTO_HMACNAME, ( void * ) c );
    lua_pushstring ( L, s );
    return 1;
}

static int hmac_gc ( lua_State *L )
{
    HANDLER_HMAC *c = hmac_pget ( L, 1 );
    HMAC_CLEANUP ( c );
    return 1;
}

static int hmac_fdigest ( lua_State *L )
{
    HANDLER_HMAC c;
    size_t written = 0;
    const char *t = luaL_checkstring ( L, 1 );
    size_t s_len;
    const char *s = luaL_checklstring ( L, 2, &s_len );
    size_t k_len;
    const char *k = luaL_checklstring ( L, 3, &k_len );
    DIGEST_TYPE type = DIGEST_BY_NAME ( t );
#if CRYPTO_OPENSSL
    unsigned char digest[EVP_MAX_MD_SIZE];
#elif CRYPTO_GCRYPT
    unsigned char *digest;
#endif

    if ( IS_DIGEST_INVALID ( type ) ) {
        luaL_argerror ( L, 1, "invalid digest type" );
        return 0;
    }

#if CRYPTO_OPENSSL
    HMAC_CTX_init ( &c );
    HMAC_Init_ex ( &c, k, k_len, type, NULL );
    HMAC_Update ( &c, ( unsigned char * ) s, s_len );
    HMAC_Final ( &c, digest, ( unsigned int * ) &written );
#elif CRYPTO_GCRYPT
    gcry_md_open ( &c, type, GCRY_MD_FLAG_HMAC );
    gcry_md_setkey ( c, k, k_len );
    gcry_md_write ( c, s, s_len );
    gcry_md_final ( c );
    digest = gcry_md_read ( c, type );
    written = gcry_md_get_algo_dlen ( type );
#endif

    if ( lua_toboolean ( L, 4 ) ) {
        lua_pushlstring ( L, ( char * ) digest, written );

    } else {
        char *hex = bin2hex ( digest, written );
        lua_pushlstring ( L, hex, written * 2 );
        free ( hex );
    }

#if CRYPTO_GCRYPT
    gcry_md_close ( c );
#endif
    return 1;
}

#if CRYPTO_OPENSSL
static int rand_do_bytes ( lua_State *L, int ( *bytes ) ( unsigned char *, int ) )
{
    size_t count = luaL_checkint ( L, 1 );
    unsigned char tmp[256], *buf = tmp;

    if ( count > sizeof tmp ) {
        buf = malloc ( count );
    }

    if ( !buf ) {
        return luaL_error ( L, "out of memory" );

    } else if ( !bytes ( buf, count ) ) {
        return crypto_error ( L );
    }

    lua_pushlstring ( L, ( char * ) buf, count );

    if ( buf != tmp ) {
        free ( buf );
    }

    return 1;
}

static int rand_bytes ( lua_State *L )
{
    return rand_do_bytes ( L, RAND_bytes );
}

static int rand_pseudo_bytes ( lua_State *L )
{
    return rand_do_bytes ( L, RAND_pseudo_bytes );
}
#elif CRYPTO_GCRYPT
static int rand_do_bytes ( lua_State *L, enum gcry_random_level level )
{
    size_t count = luaL_checkint ( L, 1 );
    void *buf = gcry_random_bytes/*_secure*/ ( count, level );
    gcry_fast_random_poll();
    lua_pushlstring ( L, ( char * ) buf, count );
    return 1;
}

static int rand_bytes ( lua_State *L )
{
    return rand_do_bytes ( L, GCRY_VERY_STRONG_RANDOM );
}

static int rand_pseudo_bytes ( lua_State *L )
{
    return rand_do_bytes ( L, GCRY_STRONG_RANDOM );
}
#endif

static int rand_add ( lua_State *L )
{
    size_t num;
    const void *buf = luaL_checklstring ( L, 1, &num );
#if CRYPTO_OPENSSL
    double entropy = ( double ) luaL_optnumber ( L, 2, num );
    RAND_add ( buf, num, entropy );
#elif CRYPTO_GCRYPT
    gcry_random_add_bytes ( buf, num, -1 ); // unknown quality
#endif
    return 0;
}

static int rand_status ( lua_State *L )
{
#if CRYPTO_OPENSSL
    lua_pushboolean ( L, RAND_status() );
#elif CRYPTO_GCRYPT
    lua_pushboolean ( L, 1 ); //feature not available AFAIK
#endif
    return 1;
}

enum { WRITE_FILE_COUNT = 1024 };
static int rand_load ( lua_State *L )
{
    const char *name = luaL_optstring ( L, 1, NULL );
#if CRYPTO_OPENSSL
    char tmp[256];
    int n;

    if ( !name && ! ( name = RAND_file_name ( tmp, sizeof tmp ) ) ) {
        return crypto_error ( L );
    }

    n = RAND_load_file ( name, WRITE_FILE_COUNT );

    if ( n == 0 ) {
        return crypto_error ( L );
    }

    lua_pushnumber ( L, n );
#elif CRYPTO_GCRYPT

    if ( name != NULL ) {
        gcry_control ( GCRYCTL_SET_RANDOM_SEED_FILE, name );
    }

    lua_pushnumber ( L, 0.0 );
#endif
    return 1;
}

static int rand_write ( lua_State *L )
{
    const char *name = luaL_optstring ( L, 1, NULL );
#if CRYPTO_OPENSSL
    char tmp[256];
    int n;

    if ( !name && ! ( name = RAND_file_name ( tmp, sizeof tmp ) ) ) {
        return crypto_error ( L );
    }

    n = RAND_write_file ( name );

    if ( n == 0 ) {
        return crypto_error ( L );
    }

    lua_pushnumber ( L, n );
#elif CRYPTO_GCRYPT
    /* this is a BUG() in gcrypt. not sure if it refers to the lib or to
      the caller, but it does not work (to set twice this file) */
    /*
    if (name != NULL)
      gcry_control(GCRYCTL_SET_RANDOM_SEED_FILE,name);
    */
    gcry_control ( GCRYCTL_UPDATE_RANDOM_SEED_FILE );
    lua_pushnumber ( L, 0.0 );
#endif
    return 1;
}

static int rand_cleanup ( lua_State *L )
{
#if CRYPTO_OPENSSL
    RAND_cleanup();
#elif CRYPTO_GCRYPT
    /* not completely sure there is nothing to do here... */
#endif
    return 0;
}

/*
** Create a metatable and leave it on top of the stack.
*/
LUACRYPTO_API int luacrypto_createmeta ( lua_State *L, const char *name,
        const luaL_reg *methods )
{
    if ( !luaL_newmetatable ( L, name ) ) {
        return 0;
    }

    /* define methods */
    luaL_openlib ( L, NULL, methods, 0 );
    /* define metamethods */
    lua_pushliteral ( L, "__index" );
    lua_pushvalue ( L, -2 );
    lua_settable ( L, -3 );
    lua_pushliteral ( L, "__metatable" );
    lua_pushliteral ( L, LUACRYPTO_PREFIX"you're not allowed to get this metatable" );
    lua_settable ( L, -3 );
    return 1;
}

/*
** Create metatables for each class of object.
*/
static void create_metatables ( lua_State *L )
{
    struct luaL_reg evp_functions[] = {
        { "digest", evp_fdigest },
        { "new", evp_fnew },
        {NULL, NULL},
    };
    struct luaL_reg evp_methods[] = {
        { "__tostring", evp_tostring },
        { "__gc", evp_gc },
        { "clone", evp_clone },
        { "digest", evp_digest },
        { "reset", evp_reset },
        { "tostring", evp_tostring },
        { "update", evp_update },
        {NULL, NULL},
    };
    struct luaL_reg hmac_functions[] = {
        { "digest", hmac_fdigest },
        { "new", hmac_fnew },
        { NULL, NULL }
    };
    struct luaL_reg hmac_methods[] = {
        { "__tostring", hmac_tostring },
        { "__gc", hmac_gc },
        { "clone", hmac_clone },
        { "digest", hmac_digest },
        { "reset", hmac_reset },
        { "tostring", hmac_tostring },
        { "update", hmac_update },
        { NULL, NULL }
    };
    struct luaL_reg rand_functions[] = {
        { "bytes", rand_bytes },
        { "pseudo_bytes", rand_pseudo_bytes },
        { "add", rand_add },
        { "seed", rand_add },
        { "load", rand_load },
        { "write", rand_write },
        { "status", rand_status },
        { "cleanup", rand_cleanup },
        { NULL, NULL }
    };
    luaL_openlib ( L, LUACRYPTO_EVPNAME, evp_functions, 0 );
    luacrypto_createmeta ( L, LUACRYPTO_EVPNAME, evp_methods );
    luaL_openlib ( L, LUACRYPTO_HMACNAME, hmac_functions, 0 );
    luacrypto_createmeta ( L, LUACRYPTO_HMACNAME, hmac_methods );
    luaL_openlib ( L, LUACRYPTO_RANDNAME, rand_functions, 0 );
    lua_pop ( L, 3 );
}

/*
** Define the metatable for the object on top of the stack
*/
LUACRYPTO_API void luacrypto_setmeta ( lua_State *L, const char *name )
{
    luaL_getmetatable ( L, name );
    lua_setmetatable ( L, -2 );
}

/*
** Assumes the table is on top of the stack.
*/
LUACRYPTO_API void luacrypto_set_info ( lua_State *L )
{
    lua_pushliteral ( L, "_COPYRIGHT" );
    lua_pushliteral ( L, "Copyright (C) 2005-2006 Keith Howe" );
    lua_settable ( L, -3 );
    lua_pushliteral ( L, "_DESCRIPTION" );
    lua_pushliteral ( L, "LuaCrypto is a Lua wrapper for OpenSSL/gcrypt" );
    lua_settable ( L, -3 );
    lua_pushliteral ( L, "_VERSION" );
    lua_pushliteral ( L, "LuaCrypto 0.3.0" );
    lua_settable ( L, -3 );
    lua_pushliteral ( L, "_ENGINE" );
    lua_pushliteral ( L, LUACRYPTO_ENGINE );
    lua_settable ( L, -3 );
}

/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUACRYPTO_API int luaopen_crypto ( lua_State *L )
{
#if CRYPTO_OPENSSL

    if ( OPENSSL_VERSION_NUMBER < 0x000907000L ) {
        return luaL_error ( L, "OpenSSL version is too old; requires 0.9.7 or higher" );
    }

    OpenSSL_add_all_digests();
#elif CRYPTO_GCRYPT
    gcry_check_version ( "1.2.2" );
    gcry_control ( GCRYCTL_DISABLE_SECMEM, 0 );
    gcry_control ( GCRYCTL_INITIALIZATION_FINISHED, 0 );
#endif
    struct luaL_reg core[] = {
        {NULL, NULL},
    };
    create_metatables ( L );
    luaL_openlib ( L, LUACRYPTO_CORENAME, core, 0 );
    luacrypto_set_info ( L );
    return 1;
}