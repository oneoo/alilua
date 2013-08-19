#include "network.h"
#include <sys/stat.h>

static char temp_buf[8192];

int network_sendfile ( epdata_t *epd, const char *path )
{
    if ( epd->process_timeout == 1 ) {
        return 0;
    }

    struct stat st;

    if ( ( epd->response_sendfile_fd = open ( path, O_RDONLY ) ) < 0 ) {
        epd->response_sendfile_fd = -2;
        //printf ( "Can't open '%s' file\n", path );
        return 0;
    }

    if ( fstat ( epd->response_sendfile_fd, &st ) == -1 ) {
        close ( epd->response_sendfile_fd );
        epd->response_sendfile_fd = -2;
        //printf ( "Can't stat '%s' file\n", path );
        return 0;
    }

    epd->response_content_length = st.st_size;
    epd->response_buf_sended = 0;

    /// clear send bufs;!!!
    int i = 0;

    for ( i = 1; i < epd->iov_buf_count; i++ ) {
        free ( epd->iov[i].iov_base );
        epd->iov[i].iov_base = NULL;
        epd->iov[i].iov_len = 0;
    }

    epd->iov_buf_count = 0;

    sprintf ( temp_buf, "Content-Type: %s", get_mime_type ( path ) );
    network_send_header ( epd, temp_buf );

    if ( temp_buf[14] == 't' && temp_buf[15] == 'e' ) {
        int fd = epd->response_sendfile_fd;
        epd->response_sendfile_fd = -1;
        epd->response_content_length = 0;
        int n = 0;

        while ( ( n = read ( fd, &temp_buf, 4096 ) ) > 0 ) {
            network_send ( epd, temp_buf, n );
        }

        if ( n < 0 ) {
        }

        close ( fd );

        return 1;
    }

#ifdef linux
    int set = 1;
    setsockopt ( epd->fd, IPPROTO_TCP, TCP_CORK, &set, sizeof ( int ) );
#endif
    return 1;
}
