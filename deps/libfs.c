/* -*-mode: C++; style: K&R; c-basic-offset: 4 ; -*- */

/**
 *  libfs.c
 *   Simple filesystem primitives for use by the Lua 5.0 scripting engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General  Public License (LGPL)
 *  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Steve Kemp
 *  ---
 *  http://www.steve.org.uk/
 *
 *  VERSION 1.12 2008-03-06 sieler (sieler@allegro.com) revision notes:
 *    Fixed possible problem in oct2decimal not noticing some illegal values.
 *    Added ability to compile on HP 3000 running MPE/iX.
 *    Added internal RELEASE define in case Makefile didn't supply it.
 *    Made getVersion static.
 *    Revised pChdir & pChmod as an example of straight-line coding.
 *    Revised luaopen_libfs for Lua 5.1 (although luaL_openlib still exists,
 *    contrary to Lua 5.1 documentation).
 *    Changed most double blank lines to single blank for consistency and
 *    readability.
 *    Separated some comments from source code for readability.
 *    (http://www.allegro.com/papers/htpp.html, even though that's for Pascal,
 *    the principles apply to C and other languages)
 */

#ifdef __hp3000s900
/* both HP C and gcc on MPE have __hp3000s900 predefined */
#define _POSIX_SOURCE 1
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define __USE_BSD 1
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <pwd.h>

#define MYNAME      "libfs"

#include "lua.h"
#include "lauxlib.h"

#ifndef RELEASE
#  define RELEASE "0.3"
#endif

/**
 * Find and return the version identifier from our CVS marker.
 * @return A static memory buffer containing the version number
 */

static char *getVersion( )
{
    static char marker[] = { "libfs.c v" RELEASE };
    return ( marker );
}


/**
 *  Convert an inode protection mode to a string.
 * @param mode The mode to convert.
 * @return A human readable notion of what the mode represents.
 */

static const char *mode2string ( mode_t mode )
{
    if ( S_ISREG ( mode ) ) {
        return "file";

    } else if ( S_ISDIR ( mode ) ) {
        return "directory";

    } else if ( S_ISLNK ( mode ) ) {
        return "link";
    }

#ifdef S_ISSOCK

    else if ( S_ISSOCK ( mode ) ) {
        return "socket";
    }

#endif

    else if ( S_ISFIFO ( mode ) ) {
        return "named pipe";

    } else if ( S_ISCHR ( mode ) ) {
        return "char device";

    } else if ( S_ISBLK ( mode ) ) {
        return "block device";

    } else {
        return "other";
    }
}

/**
 * Convert the given "octal" value to a decimal integer.
 * Unlike most octal -> decimal routines, this routine does not
 * take an ASCII string of digits ... it's input is an integer.
 * The "octal value" is an integer of the form abcde (e.g, 12345)
 * This routine takes each decimal digit and re-interprets it
 * as an octal digit (thus, 12345 --> 012345).
 *
 * Note: only non-negative values are allowed.
 *
 * @param octal The value to convert.
 * @return The decimal representation (>= 0), or -1 if failure.
 */

static int oct2decimal ( int octal )
{
    char val [21];         /* large enough for any 64-bit integer */
    int decval = 0;
    int i;
    sprintf ( val, "%d", octal );

    for ( i = 0; i < strlen ( val ); i ++ ) {
        int octval = val [i];
        decval *= 8;
        octval = octval - '0';

        /* check for invalid digit */

        if ( ( octval > 7 ) || ( octval < 0 ) ) {
            return -1;    /* indicates failure */
        }

        decval += octval;
    }

    return decval;
}

/**
 * Return an error string to the LUA script.
 * @param L The Lua intepretter object.
 * @param info An error string to return to the caller.  Pass NULL to use the return value of strerror.
 */

static int pusherror ( lua_State *L, const char *info )
{
    int save_errno =
        errno;      /* in case lua_pushnil() or lua_pushfstring changes errno */
    lua_pushnil ( L );

    if ( info == NULL ) {
        lua_pushstring ( L, strerror ( save_errno ) );

    } else {
        lua_pushfstring ( L, "%s: %s", info, strerror ( save_errno ) );
    }

    lua_pushnumber ( L, save_errno );
    return 3;
}

/***
 ***  Now we have the actual functions which are exported to Lua
 ***
 ***/

/**
 * Perform a chdir() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pChdir ( lua_State *L )
{
    const char *path;

    if ( ! lua_isstring ( L, 1 ) ) {
        return ( pusherror ( L, "chdir(string);" ) );
    }

    path = lua_tostring ( L, 1 );

    if ( chdir ( path ) ) {
        return ( pusherror ( L, "Failed to chdir()" ) );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

/**
 * Perform a chmod() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pChmod ( lua_State *L )
{
    const char *path;
    int   mode;

    if ( ! lua_isstring ( L, 1 ) ) {
        return pusherror ( L, "chmod(string,int);" );
    }

    path = lua_tostring ( L, 1 );

    if ( ! lua_isnumber ( L, 2 ) ) {
        return pusherror ( L, "chmod(string,int);" );
    }

    mode = oct2decimal ( lua_tonumber ( L, 2 ) );

    if ( mode < 0 ) { /* Make sure the octal conversion succeeded */
        return pusherror ( L, "invalid mode for chmod()" );
    }

    if ( chmod ( path, mode ) != 0 ) {
        return pusherror ( L, "Failed to chmod()" );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

/**
 * Perform a chown() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pChown ( lua_State *L )
{
    const char *path;
    uid_t owner = 0;
    gid_t group = 0;

    if ( lua_isstring ( L, 1 ) ) {
        path = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "chown(string,[string|int],[string|int]);" ) );
    }

    /* Owner */
    if ( lua_isnumber ( L, 2 ) ) {
        owner = ( uid_t ) lua_tonumber ( L, 2 );
    }

    if ( lua_isstring ( L, 2 ) ) {
        struct passwd *p = getpwnam ( lua_tostring ( L, 2 ) );

        if ( p != NULL ) {
            owner = p->pw_uid;

        } else {
            return ( pusherror ( L, "user not found in passwd" ) );
        }

    } else {
        return ( pusherror ( L, "chown(string,[string|int],[string|int]);" ) );
    }

    /* Group */
    if ( lua_isnumber ( L, 3 ) ) {
        group = ( gid_t ) lua_tonumber ( L, 2 );
    }

    if ( lua_isstring ( L, 3 ) ) {
        struct passwd *p = getpwnam ( lua_tostring ( L, 3 ) );

        if ( p != NULL ) {
            group = p->pw_gid;

        } else {
            return ( pusherror ( L, "user not found in passwd" ) );
        }

    } else {
        return ( pusherror ( L, "chown(string,[string|int],[string|int]);" ) );
    }

    if ( chown ( path, owner, group ) != 0 ) {
        return ( pusherror ( L, "Failed to chown()" ) );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

/**
 * Perform a cwd() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pCwd ( lua_State *L )
{
    char path[1026];
    memset ( path, '\0', sizeof ( path ) );

    if ( getcwd ( path, sizeof ( path ) - 1 ) == NULL ) {
        return ( pusherror ( L, "cwd() failed" ) );
    }

    /* Return the path */
    lua_pushstring ( L, path );
    return 1;
}

/**
 * Test to see if the given file/path is a directory.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pIsDir ( lua_State *L )
{
    const char *fileName = NULL;
    struct stat st;

    if ( lua_isstring ( L, 1 ) ) {
        fileName = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "is_dir(string)" ) );
    }

    if ( stat ( fileName, &st ) == 0 ) {
        lua_pushboolean ( L, S_ISDIR ( st.st_mode ) );

    } else {
        return ( pusherror ( L, "stat failed!" ) );
    }

    return 1;
}

/**
 * Test to see if the given file/path is a file.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pIsFile ( lua_State *L )
{
    const char *fileName = NULL;
    struct stat st;

    if ( lua_isstring ( L, 1 ) ) {
        fileName = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "is_file(string)" ) );
    }

    if ( stat ( fileName, &st ) == 0 ) {
        lua_pushboolean ( L, S_ISREG ( st.st_mode ) );

    } else {
        return ( pusherror ( L, "stat failed!" ) );
    }

    return 1;
}

/**
 * Perform a mkdir() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pMkdir ( lua_State *L )
{
    const char *path;

    if ( lua_isstring ( L, 1 ) ) {
        path = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "mkdir(string);" ) );
    }

    if ( mkdir ( path, 0777 ) != 0 ) {
        return ( pusherror ( L, "Failed to mkdir()" ) );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

/**
 * Return a table of all the directory entries in the given dir.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 * Leaves a newly created table on the Lua stack
 */

static int pReadDir ( lua_State *L )
{
    const char *dirName = NULL;
    struct dirent *dp;
    DIR *dir;
    int count = 0;

    if ( lua_isstring ( L, 1 ) ) {
        dirName = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "readdir(string)" ) );
    }

    /* creates a table/array to hold the results */
    lua_newtable ( L );
    dir = opendir ( dirName );

    while ( dir ) {
        if ( ( dp = readdir ( dir ) ) != NULL ) {
            /* Store in the table. */
            lua_pushnumber ( L, count );     /* table index */
            lua_pushstring ( L, dp->d_name ); /* value this index */
            lua_settable ( L, -3 );
            count += 1;

        } else {
            closedir ( dir );
            dir = 0;
        }
    }

    /* Make sure LUA knows how big our table is. */
    lua_pushliteral ( L, "n" );
    lua_pushnumber ( L, count - 1 );
    lua_rawset ( L, -3 );
    return 1;        /* we've left one item on the stack */
}

/**
 * Perform a rmdir() operation.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pRmdir ( lua_State *L )
{
    const char *path;

    if ( lua_isstring ( L, 1 ) ) {
        path = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "rmdir(string);" ) );
    }

    if ( rmdir ( path ) != 0 ) {
        return ( pusherror ( L, "Failed to rmdir()" ) );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

static int pUnlink ( lua_State *L )
{
    const char *path;

    if ( lua_isstring ( L, 1 ) ) {
        path = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "rmdir(string);" ) );
    }

    if ( unlink ( path ) != 0 ) {
        return ( pusherror ( L, "Failed to unlink()" ) );
    }

    /* Success */
    lua_pushboolean ( L, 1 );
    return 1;
}

/**
 * Perform a stat operation upon the given file.
 * @param L The lua intepreter object.
 * @return The number of results to be passed back to the calling Lua script.
 */

static int pStat ( lua_State *L )
{
    struct stat info;
    const char *file;
    char mode_str[10];

    if ( lua_isstring ( L, 1 ) ) {
        file = lua_tostring ( L, 1 );

    } else {
        return ( pusherror ( L, "stat(string);" ) );
    }

    if ( stat ( file, &info ) ) {
        return ( pusherror ( L, "stat() failed" ) );
    }

    sprintf ( mode_str, "%o", ( 0xfff & info.st_mode ) );
    lua_newtable ( L );
    /* device inode resides on */
    lua_pushliteral ( L, "dev" );
    lua_pushnumber ( L, ( lua_Number ) info.st_dev );
    lua_rawset ( L, -3 );
    /* inode's number */
    lua_pushliteral ( L, "ino" );
    lua_pushnumber ( L, ( lua_Number ) info.st_ino );
    lua_rawset ( L, -3 );
    /* Type of entry */
    lua_pushliteral ( L, "mode" );
    lua_pushnumber ( L,  atoi ( mode_str ) );
    lua_rawset ( L, -3 );
    /* Type of entry */
    lua_pushliteral ( L, "type" );
    lua_pushstring ( L, mode2string ( info.st_mode ) );
    lua_rawset ( L, -3 );
    /* number of hard links to the file */
    lua_pushliteral ( L, "nlink" );
    lua_pushnumber ( L, ( lua_Number ) info.st_nlink );
    lua_rawset ( L, -3 );
    /* user-id of owner */
    lua_pushliteral ( L, "uid" );
    lua_pushnumber ( L, ( lua_Number ) info.st_uid );
    lua_rawset ( L, -3 );
    /* group-id of owner */
    lua_pushliteral ( L, "gid" );
    lua_pushnumber ( L, ( lua_Number ) info.st_gid );
    lua_rawset ( L, -3 );
    /* device type, for special file inode */
    lua_pushliteral ( L, "rdev" );
    lua_pushnumber ( L, ( lua_Number ) info.st_rdev );
    lua_rawset ( L, -3 );
    /* time of last access */
    lua_pushliteral ( L, "access" );
    lua_pushnumber ( L, info.st_atime );
    lua_rawset ( L, -3 );
    /* time of last data modification */
    lua_pushliteral ( L, "modification" );
    lua_pushnumber ( L, info.st_mtime );
    lua_rawset ( L, -3 );
    /* time of last file status change */
    lua_pushliteral ( L, "change" );
    lua_pushnumber ( L, info.st_ctime );
    lua_rawset ( L, -3 );
    /* file size, in bytes */
    lua_pushliteral ( L, "size" );
    lua_pushnumber ( L, ( lua_Number ) info.st_size );
    lua_rawset ( L, -3 );
    return 1;
}

/**
 * Mappings between the LUA code and our C code.
 */

static const luaL_reg R[] = {
    {"chdir",           pChdir},
    {"chmod",           pChmod},
    {"chown",           pChown},
    {"cwd",             pCwd},
    {"is_dir",          pIsDir},
    {"is_file",         pIsFile},
    {"mkdir",           pMkdir},
    {"readdir",         pReadDir},
    {"rmdir",           pRmdir},
    {"unlink",          pUnlink},
    {"stat",            pStat},
    {NULL,      NULL}
};

/**
 * Bind our exported functions to the Lua intepretter, making our functions
 * available to the calling script.
 * @param L The lua intepreter object.
 * @return 1 (for the table we leave on the stack)
 */

LUALIB_API int luaopen_libfs ( lua_State *L )
{
    /* Version number from CVS marker. */
    char *version = getVersion();
#ifdef PRE_LUA51
    luaL_openlib ( L, MYNAME, R, 0 );
#else
    luaL_register ( L, MYNAME, R );
#endif
    lua_pushliteral ( L, "version" );
    lua_pushstring ( L, version );
    lua_settable ( L, -3 );
    return 1;
}

