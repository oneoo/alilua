#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mime {
    char *ext;
    char *type;
    void *next;
} mime_t;

static mime_t *mime_types[26];

static void add_mime_type ( char *ext, char *type )
{
    int ext_len = strlen ( ext );
    int type_len = strlen ( type );
    mime_t *n = malloc ( sizeof ( mime_t ) + ext_len + 2 + type_len );

    n->next = NULL;
    n->ext = ( char * ) n + sizeof ( mime_t );
    n->type = n->ext + ext_len + 1;

    memcpy ( n->ext, ext, ext_len );
    memcpy ( n->type, type, type_len );

    n->ext[ext_len] = '\0';
    n->type[type_len] = '\0';

    int k = tolower ( ext[0] ) - 'a';

    if ( mime_types[k] == NULL ) {
        mime_types[k] = n;

    } else {
        n->next = mime_types[k];
        mime_types[k] = n;
    }
}

void init_mime_types()
{
    int i = 0;

    for ( i = 0; i < 26; i++ ) {
        mime_types[i] = NULL;
    }

    add_mime_type ( "htm", "text/html" );
    add_mime_type ( "lua", "text/plain" );
    add_mime_type ( "txt", "text/plain" );
    add_mime_type ( "php", "text/plain" );
    add_mime_type ( "java", "text/plain" );
    add_mime_type ( "log", "text/plain" );
    add_mime_type ( "css", "text/css" );
    add_mime_type ( "js", "text/javascript" );
    add_mime_type ( "json", "application/json" );
    add_mime_type ( "html", "text/html" );
    add_mime_type ( "jpg", "image/jpeg" );
    add_mime_type ( "jpeg", "image/jpeg" );
    add_mime_type ( "gif", "image/gif" );
    add_mime_type ( "png", "image/png" );
    add_mime_type ( "ico", "image/icon" );
    add_mime_type ( "woff", "application/x-woff" );
    add_mime_type ( "ttf", "font/truetype" );
    add_mime_type ( "otf", "font/opentype" );
}

const char *
get_mime_type ( const char *filename )
{
    int i = 0;
    const char *ext = filename;
    int l = strlen ( filename );

    for ( i = l; i--; ) {
        if ( ext[i] == '.' ) {
            ext = &ext[i + 1];

            break;
        }
    }

    char *t = "text/plain";
    mime_t *u = mime_types[tolower ( ext[0] ) - 'a'];

    while ( u ) {
        if ( stricmp ( u->ext, ext ) == 0 ) {
            return u->type;
        }

        u = u->next;
    }

    return t;
}
