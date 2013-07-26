#include "network.h"

int is_binary ( const char *buf, int buf_len )
{
    int suspicious_bytes = 0;
    int total_bytes = buf_len > 1024 ? 1024 : buf_len;
    const unsigned char *buf_c = buf;
    int i;

    if ( buf_len == 0 ) {
        return 0;
    }

    if ( buf_len >= 3 && buf_c[0] == 0xEF && buf_c[1] == 0xBB && buf_c[2] == 0xBF ) {
        /* UTF-8 BOM. This isn't binary. */
        return 0;
    }

    for ( i = 0; i < total_bytes; i++ ) {
        /* Disk IO is so slow that it's worthwhile to do this calculation after every suspicious byte. */
        /* This is true even on a 1.6Ghz Atom with an Intel 320 SSD. */
        /* Read at least 32 bytes before making a decision */
        if ( i >= 32 && ( suspicious_bytes * 100 ) / total_bytes > 10 ) {
            return 1;
        }

        if ( buf_c[i] == '\0' ) {
            /* NULL char. It's binary */
            return 1;

        } else if ( ( buf_c[i] < 7 || buf_c[i] > 14 ) && ( buf_c[i] < 32 || buf_c[i] > 127 ) ) {
            /* UTF-8 detection */
            if ( buf_c[i] > 191 && buf_c[i] < 224 && i + 1 < total_bytes ) {
                i++;

                if ( buf_c[i] < 192 ) {
                    continue;
                }

            } else if ( buf_c[i] > 223 && buf_c[i] < 239 && i + 2 < total_bytes ) {
                i++;

                if ( buf_c[i] < 192 && buf_c[i + 1] < 192 ) {
                    i++;
                    continue;
                }
            }

            suspicious_bytes++;
        }
    }

    if ( ( suspicious_bytes * 100 ) / total_bytes > 10 ) {
        return 1;
    }
}
