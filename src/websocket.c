#include "network.h"
#include <lua.h>

static unsigned char buf_256[256];
int ws_send_data ( epdata_t *epd,
                   unsigned int fin,
                   unsigned int rsv1,
                   unsigned int rsv2,
                   unsigned int rsv3,
                   unsigned int opcode,
                   uint64_t payload_len,
                   const char *payload_data )
{

    unsigned int offset = 0;


    /*
     * Per protocol spec on RFC6455, when the server sends a websocket
     * frame to the client it must NOT be masked:
     *
     * 5.  Data Framing
     *
     * 5.1.  Overview
     * .......
     * ....... A server MUST NOT mask any frames that it sends to
     * the client.  A client MUST close a connection if it detects a masked
     * frame....
     */
    unsigned int frame_mask = 0;
    int n;

    memset ( buf_256, 0, 256 );
    buf_256[0] |= ( ( fin << 7 ) | ( rsv1 << 6 ) | ( rsv2 << 5 ) | ( rsv3 << 4 ) | opcode );

    if ( payload_len < 126 ) {
        buf_256[1] |= ( ( frame_mask << 7 ) | payload_len );
        offset = 2;

    } else if ( payload_len >= 126 && payload_len <= 0xFFFF ) {
        void *p = &buf_256[2];
        buf_256[1] = 126;
        * ( uint16_t * ) p = htons ( ( uint16_t ) payload_len );
        offset = 4;

    } else {
        buf_256[1] |= ( ( frame_mask << 7 ) | 127 );
        memcpy ( buf_256 + 2, &payload_len, 8 );
        offset = 10;
    }

    update_timeout ( epd->timeout_ptr, STEP_WS_SEND_TIMEOUT );

    if ( offset + payload_len < 256 ) {
        memcpy ( buf_256 + offset, payload_data, payload_len );

        n = network_raw_send ( epd->fd, buf_256, offset + payload_len );

        if ( n <= 0 ) {
            return -1;
        }

        return n;

    } else {
        n = network_raw_send ( epd->fd, buf_256, offset );

        if ( n <= 1 ) {
            return -1;
        }

        epd->websocket->data = ( char * ) payload_data;
        epd->websocket->data_len = payload_len;
        //printf ( "bewrite %d\n", epd->websocket->data_len );
        epd->websocket->sended = 0; // for sended count

        se_be_write ( epd->se_ptr, websocket_be_write );

        return 1;
    }
}

/*
 * @METHOD_NAME: write
 * @METHOD_DESC: It writes a message frame to a specified websocket connection.
 * @METHOD_PROTO: int write(struct ws_request *wr, unsigned int code, unsigned char *data, uint64_t len)
 * @METHOD_PARAM: wr the target websocket connection
 * @METHOD_PARAM: code the message code, can be one of: WS_OPCODE_CONTINUE, WS_OPCODE_TEXT, WS_OPCODE_BINARY, WS_OPCODE_CLOSE, WS_OPCODE_PING or WS_OPCODE_PONG.
 * @METHOD_PARAM: data the data to be send
 * @METHOD_PARAM: len the length of the data to be send
 * @METHOD_RETURN:  Upon successful completion it returns 0, on error returns -1.
 */
int ws_write ( epdata_t *epd, unsigned int code, unsigned char *data, uint64_t len )
{
    return ws_send_data ( epd, 1, 0, 0, 0, code, len, data );
}

int websocket_be_read ( se_ptr_t *ptr )
{
    epdata_t *epd = ptr->data;

    if ( !epd ) {
        return 0;
    }

    int n = 0;
    update_timeout ( epd->timeout_ptr, STEP_WS_READ_TIMEOUT );

    if ( epd->headers == NULL ) {
        epd->headers = malloc ( 4096 );

        if ( epd->headers == NULL ) {
            epd->status = STEP_FINISH;
            //network_send_error ( epd, 503, "memory error!" );
            close_client ( epd );
            serv_status.reading_counts--;
            return 0;
        }

        epd->buf_size = 4096;

    } else if ( epd->data_len + 4096 > epd->buf_size ) {
        unsigned char *_t = ( unsigned char * ) realloc ( epd->headers, epd->buf_size + 4096 );

        if ( _t != NULL ) {
            epd->headers = _t;

        } else {
            close_client ( epd );
            serv_status.reading_counts--;
            return 0;
        }

        epd->buf_size += 4096;
    }

    while ( ( n = recv ( epd->fd, epd->headers + epd->data_len,
                         epd->buf_size - epd->data_len, 0 ) ) >= 0 ) {
        if ( n == 0 ) {
            close_client ( epd );
            epd = NULL;
            break;
        }

        if ( epd->data_len + n >= epd->buf_size ) {
            unsigned char *_t = ( unsigned char * ) realloc ( epd->headers, epd->buf_size + 4096 );

            if ( _t != NULL ) {
                epd->headers = _t;

            } else {
                //network_send_error ( epd, 503, "buf error!" );
                close_client ( epd );
                epd = NULL;
                serv_status.reading_counts--;
                break;
            }

            epd->buf_size += 4096;
        }

        if ( epd->status != STEP_READ ) {
            serv_status.reading_counts++;
            epd->status = STEP_READ;
            epd->data_len = n;
            epd->start_time = longtime();

        } else {
            epd->data_len += n;
        }

        //printf ( "readed: %d / %d\n", n, epd->data_len );

        if ( epd->data_len >= 2 && epd->content_length == -1 ) {
            uint frame_opcode   = epd->headers[0] & 0x0f;
            uint64_t payload_length = epd->headers[1] & 0x7f;
            // #define CHECK_BIT(var, pos)        !!((var) & (1 << (pos)))
            epd->websocket->frame_mask     = !! ( ( epd->headers[1] ) & ( 1 << ( 7 ) ) );

            epd->websocket->masking_key_offset = 0;

            if ( payload_length == 126 ) {
                payload_length = epd->headers[2] * 256 + epd->headers[3];
                epd->websocket->masking_key_offset = 4;

            } else if ( payload_length == 127 ) {
                memcpy ( &payload_length, epd->headers + 2, 8 );
                epd->websocket->masking_key_offset = 10;

            } else {
                epd->websocket->masking_key_offset = 2;
            }

            //printf ( "get a frame %d\n", payload_length );
            epd->content_length = payload_length + epd->websocket->masking_key_offset + 4;

            if ( !epd->websocket->frame_mask ) {
                epd->content_length -= 4;
            }

            if ( frame_opcode == WS_OPCODE_CLOSE ) {
                ws_send_data ( epd, 1, 0, 0, 0, WS_OPCODE_CLOSE, 0, NULL );
                close_client ( epd );
                epd = NULL;
                serv_status.reading_counts--;
                return 0;
            }
        }

        if ( epd->content_length > -1 ) {
            if ( epd->data_len >= epd->content_length ) {
                uint64_t payload_length = epd->content_length - epd->websocket->masking_key_offset;
                epd->content_length = -1;

                if ( epd->websocket->frame_mask ) {
                    memcpy ( epd->websocket->frame_masking_key,
                             epd->headers + epd->websocket->masking_key_offset, 4 );
                    epd->contents = epd->headers + epd->websocket->masking_key_offset + 4;
                    payload_length -= 4;
                    int i = 0;

                    for ( i = 0; i < payload_length; i++ ) {
                        epd->contents[i] = epd->contents[i] ^ epd->websocket->frame_masking_key[i & 0x03];
                    }

                } else {
                    epd->contents = epd->headers + epd->websocket->masking_key_offset;
                }

                //printf ( "%ld readed %ld\n", epd->content_length, epd->data_len );
                epd->data_len = 0;
                int k = epd->contents[payload_length - 1];
                epd->contents[payload_length - 1] = '\0';

                epd->contents[payload_length - 1] = k;
                lua_State *L = ( lua_State * ) epd->websocket->ML;

                lua_rawgeti ( L, LUA_REGISTRYINDEX, epd->websocket->websocket_handles );
                lua_pushstring ( L, "on" );
                lua_gettable ( L, -2 );

                if ( lua_isfunction ( L, -1 ) ) {
                    lua_pushlstring ( L, epd->contents, payload_length );

                    if ( lua_pcall ( L, 1, 0, 0 ) ) {
                        if ( lua_isstring ( L, -1 ) ) {
                            errorlog ( epd, lua_tostring ( L, -1 ) );
                            lua_pop ( L, 1 );
                        }
                    }

                    lua_pop ( L, 1 );
                }
            }
        }
    }

    if ( epd && n < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) {
        //printf("error fd %d (%d) %s\n", epd->fd, errno, strerror(errno));
        close_client ( epd );
        epd = NULL;
        return 0;
    }

    return 1;
}

int websocket_be_write ( se_ptr_t *ptr )
{
    epdata_t *epd = ptr->data;

    if ( !epd ) {
        return 0;
    }

    int n = 0;
    //printf ( "bewrite %d\n", epd->websocket->data_len );

    while ( epd->websocket->sended < epd->websocket->data_len ) {
        n = send ( epd->fd, epd->websocket->data + epd->websocket->sended,
                   epd->websocket->data_len - epd->websocket->sended, MSG_DONTWAIT );

        if ( n < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return 0;

            } else {
                close_client ( epd );
                epd = NULL;
                lua_State *L = ( lua_State * ) epd->websocket->L;
                lua_pushnil ( L );
                lua_pushstring ( L, "send error!" );
                lua_resume ( L, 2 );
                return 0;
            }

        } else {
            epd->websocket->sended += n;
        }
    }

    if ( epd->websocket->sended >= epd->websocket->data_len ) {
        epd->websocket->data = NULL;
        epd->websocket->data_len = -1;
        //printf ( "send finish %d\n", epd->websocket->data_len );
        se_be_read ( epd->se_ptr, websocket_be_read );
        lua_State *L = ( lua_State * ) epd->websocket->L;
        lua_pushboolean ( L, 1 );
        lua_resume ( L, 1 );
        return 0;
    }

    return 1;
}