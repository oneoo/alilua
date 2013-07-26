#include "network.h"

static char temp_buf[8192];

int gzip_iov ( int mode, struct iovec *iov, int iov_count, int *_diov_count )
{
    //char buf[EP_D_BUF_SIZE];
    char *buf = temp_buf;
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;
    int ret = deflateInit2 ( &stream,
                             GZIP_LEVEL, // gzip level
                             Z_DEFLATED,
                             -MAX_WBITS,
                             8,
                             Z_DEFAULT_STRATEGY );

    if ( Z_OK != ret ) {
        printf ( "deflateInit error: %d\r\n", ret );
        return 0;
    }

    /// iov[0] is header block , do not gzip!!!
    int i = 1, dlen = 0, ilen = 0, diov_count = 1, zed = 0, cpd = 0;
    unsigned crc = 0L;

    int flush = 0;

    do {
        if ( i >= _MAX_IOV_COUNT || !iov[i].iov_base ) {
            break;
        }

        ilen += iov[i].iov_len;

        if ( mode == 1 ) { /// deflate mode , not need crc
            crc = crc32 ( crc, iov[i].iov_base, iov[i].iov_len );
        }

        stream.next_in = iov[i].iov_base;
        stream.avail_in = iov[i].iov_len;
        iov[i].iov_len = 0;

        flush = ( i == iov_count || i + 1 >= _MAX_IOV_COUNT || iov[i + 1].iov_base == NULL
                ) ? Z_FINISH : Z_NO_FLUSH;
        i++;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            stream.avail_out = EP_D_BUF_SIZE;
            stream.next_out = buf;
            ret = deflate ( &stream, flush ); /* no bad return value */

            if ( ret == Z_STREAM_ERROR ) {
                deflateEnd ( &stream );
                printf ( "Z_STREAM_ERROR | Z_MEM_ERROR\n" );
                return 0;
            }

            zed = EP_D_BUF_SIZE - stream.avail_out;

            if ( zed > 0 ) {
                /// send at header block !!!!! not here
                /*if(diov_count == 1){
                    memcpy(iov[diov_count].iov_base, gzip_header, 10);
                    iov[diov_count].iov_len = 10;
                    dlen += 10;
                }*/
                dlen += zed;

                if ( zed + iov[diov_count].iov_len <= EP_D_BUF_SIZE ) {
                    memcpy ( iov[diov_count].iov_base + iov[diov_count].iov_len, buf, zed );
                    iov[diov_count].iov_len += zed;

                    if ( iov[diov_count].iov_len == EP_D_BUF_SIZE ) {
                        diov_count++;

                        if ( iov[diov_count].iov_base == NULL ) {
                            deflateEnd ( &stream );
                            printf ( "gzip: iov buf count error!\n" );
                            return 0;
                        }
                    }

                } else {
                    cpd = EP_D_BUF_SIZE - iov[diov_count].iov_len;

                    if ( cpd > 0 ) {
                        memcpy ( iov[diov_count].iov_base + iov[diov_count].iov_len, buf, cpd );
                        iov[diov_count].iov_len += cpd;

                        if ( zed == cpd ) {
                            continue;
                        }
                    }

                    if ( diov_count + 1 < i && iov[diov_count + 1].iov_base != NULL ) {
                        diov_count++;
                        memcpy ( iov[diov_count].iov_base, buf + cpd, zed - cpd );
                        iov[diov_count].iov_len = zed - cpd;

                    } else {
                        deflateEnd ( &stream );
                        printf ( "gzip: iov buf count error!\n" );
                        return 0;
                    }
                }
            }
        } while ( stream.avail_out == 0 );

        /* done when last data in file processed */
    } while ( flush != Z_FINISH );

    /* clean up and return */
    deflateEnd ( &stream );

    if ( mode == 1 ) {
        char *o = ( ( char * ) iov[diov_count].iov_base );
        o[iov[diov_count].iov_len++] = ( crc & 0xff );
        o[iov[diov_count].iov_len++] = ( ( crc >> 8 ) & 0xff );
        o[iov[diov_count].iov_len++] = ( ( crc >> 16 ) & 0xff );
        o[iov[diov_count].iov_len++] = ( ( crc >> 24 ) & 0xff );

        o[iov[diov_count].iov_len++] = ( ilen & 0xff );
        o[iov[diov_count].iov_len++] = ( ( ilen >> 8 ) & 0xff );
        o[iov[diov_count].iov_len++] = ( ( ilen >> 16 ) & 0xff );
        o[iov[diov_count].iov_len++] = ( ( ilen >> 24 ) & 0xff );

        dlen += 8;
    }

    int j = diov_count + 1;

    for ( ; j < i; j++ ) {
        free ( iov[j].iov_base );
        iov[j].iov_base = NULL;
        iov[j].iov_len = 0;
    }

    *_diov_count = diov_count;

    return dlen;
}
