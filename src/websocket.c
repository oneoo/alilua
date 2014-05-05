#include "main.h"
#include "config.h"
#include "network.h"
#include "websocket.h"

static unsigned char buf_256[256];
static uint64_t ntohll(uint64_t n)
{
#if BYTE_ORDER == LITTLE_ENDIAN
    n = ((n << 8) & 0xFF00FF00FF00FF00ULL) |
        ((n >> 8) & 0x00FF00FF00FF00FFULL);
    n = ((n << 16) & 0xFFFF0000FFFF0000ULL) |
        ((n >> 16) & 0x0000FFFF0000FFFFULL);
    n = (n << 32) | (n >> 32);
#endif
    return n;
}

int websocket_be_read(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0;
    update_timeout(epd->timeout_ptr, STEP_WS_READ_TIMEOUT);

    if(epd->headers == NULL) {
        epd->headers = malloc(4096);

        if(epd->headers == NULL) {
            epd->status = STEP_FINISH;
            //network_send_error ( epd, 503, "memory error!" );
            //close_client(epd);
            se_delete(epd->se_ptr);
            epd->se_ptr = NULL;
            delete_timeout(epd->timeout_ptr);
            epd->timeout_ptr = NULL;

            if(epd->fd > -1) {
                serv_status.active_counts--;
                close(epd->fd);
                epd->fd = -1;
            }

            //serv_status.reading_counts--;
            return 0;
        }

        epd->buf_size = 4096;

    } else if(epd->data_len + 4096 > epd->buf_size) {
        unsigned char *_t = (unsigned char *) realloc(epd->headers, epd->buf_size + 4096);

        if(_t != NULL) {
            epd->headers = _t;

        } else {
            //close_client(epd);
            se_delete(epd->se_ptr);
            epd->se_ptr = NULL;
            delete_timeout(epd->timeout_ptr);
            epd->timeout_ptr = NULL;

            if(epd->fd > -1) {
                serv_status.active_counts--;
                close(epd->fd);
                epd->fd = -1;
            }

            return 0;
        }

        epd->buf_size += 4096;
    }

    while((n = recv(epd->fd, epd->headers + epd->data_len,
                    epd->buf_size - epd->data_len, 0)) >= 0) {
        if(n == 0) {
            //close_client(epd);
            //epd = NULL;
            se_delete(epd->se_ptr);
            epd->se_ptr = NULL;
            delete_timeout(epd->timeout_ptr);
            epd->timeout_ptr = NULL;

            if(epd->fd > -1) {
                serv_status.active_counts--;
                close(epd->fd);
                epd->fd = -1;
            }

            break;
        }

        if(epd->data_len + n >= epd->buf_size) {
            unsigned char *_t = (unsigned char *) realloc(epd->headers, epd->buf_size + 4096);

            if(_t != NULL) {
                epd->headers = _t;

            } else {
                //network_send_error ( epd, 503, "buf error!" );
                //close_client(epd);
                //epd = NULL;
                se_delete(epd->se_ptr);
                epd->se_ptr = NULL;
                delete_timeout(epd->timeout_ptr);
                epd->timeout_ptr = NULL;

                if(epd->fd > -1) {
                    serv_status.active_counts--;
                    close(epd->fd);
                    epd->fd = -1;
                }

                break;
            }

            epd->buf_size += 4096;
        }

        if(epd->status != STEP_READ) {
            serv_status.reading_counts++;
            epd->status = STEP_READ;
            epd->data_len = n;

        } else {
            epd->data_len += n;
        }

        //printf ( "readed: %d / %d\n", n, epd->data_len );

        if(epd->data_len >= 2 && epd->content_length == -1) {
            uint frame_opcode   = epd->headers[0] & 0x0f;
            uint64_t payload_length = epd->headers[1] & 0x7f;
            // #define CHECK_BIT(var, pos)        !!((var) & (1 << (pos)))
            epd->websocket->frame_mask     = !!((epd->headers[1]) & (1 << (7)));

            epd->websocket->masking_key_offset = 0;

            if(payload_length == 126) {
                payload_length = epd->headers[2] * 256 + epd->headers[3];
                epd->websocket->masking_key_offset = 4;

            } else if(payload_length == 127) {
                memcpy(&payload_length, epd->headers + 2, 8);
                epd->websocket->masking_key_offset = 10;

            } else {
                epd->websocket->masking_key_offset = 2;
            }

            //printf ( "get a frame %d\n", payload_length );
            epd->content_length = payload_length + epd->websocket->masking_key_offset + 4;

            if(!epd->websocket->frame_mask) {
                epd->content_length -= 4;
            }

            if(frame_opcode == WS_OPCODE_CLOSE) {
                ws_send_data(epd, 1, 0, 0, 0, WS_OPCODE_CLOSE, 0, NULL);
                epd->websocket->ended = 1;

                return 1;
            }
        }

        if(epd->content_length > -1) {
            if(epd->data_len >= epd->content_length) {
                uint frame_opcode = epd->headers[0] & 0x0f;
                uint64_t payload_length = epd->content_length - epd->websocket->masking_key_offset;
                epd->content_length = -1;

                if(epd->websocket->frame_mask) {
                    memcpy(epd->websocket->frame_masking_key,
                           epd->headers + epd->websocket->masking_key_offset, 4);
                    epd->contents = epd->headers + epd->websocket->masking_key_offset + 4;
                    payload_length -= 4;
                    int i = 0;

                    for(i = 0; i < payload_length; i++) {
                        epd->contents[i] = epd->contents[i] ^ epd->websocket->frame_masking_key[i & 0x03];
                    }

                } else {
                    epd->contents = epd->headers + epd->websocket->masking_key_offset;
                }

                epd->status = STEP_WAIT;
                serv_status.reading_counts--;

                //printf ( "%ld readed %ld\n", epd->content_length, epd->data_len );
                epd->data_len = 0;
                int k = epd->contents[payload_length - 1];
                epd->contents[payload_length - 1] = '\0';

                epd->contents[payload_length - 1] = k;

                lua_State *L = epd->L;
                lua_getglobal(L, "__websocket_on__");

                if(lua_isfunction(L, -1)) {
                    lua_pushlstring(L, epd->contents, payload_length);
                    lua_pushnumber(L, frame_opcode);
                    lua_pushboolean(L, !!((epd->headers[0]) & (1 << (7))));

                    if(lua_pcall(L, 3, 0, 0)) {
                        if(lua_isstring(L, -1)) {
                            LOGF(ERR, "%s", lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                }

            }
        }
    }

    if(epd && n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        //printf("error fd %d (%d) %s\n", epd->fd, errno, strerror(errno));

        epd->websocket->ended = 1;

        return 0;
    }

    return 1;
}

int websocket_be_write(se_ptr_t *ptr)
{
    epdata_t *epd = ptr->data;

    if(!epd) {
        return 0;
    }

    int n = 0;
    //printf ( "bewrite %d\n", epd->websocket->data_len );

    while(epd->websocket->sended < epd->websocket->data_len) {
        n = send(epd->fd, epd->websocket->data + epd->websocket->sended, epd->websocket->data_len - epd->websocket->sended,
                 MSG_DONTWAIT);

        if(n < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;

            } else {
                //close_client(epd);
                //epd = NULL;
                se_delete(epd->se_ptr);
                epd->se_ptr = NULL;
                delete_timeout(epd->timeout_ptr);
                epd->timeout_ptr = NULL;

                if(epd->fd > -1) {
                    serv_status.active_counts--;
                    close(epd->fd);
                    epd->fd = -1;
                }

                lua_State *L = (lua_State *) epd->L;
                lua_pushnil(L);
                lua_pushstring(L, "send error!");
                lua_resume(L, 2);
                return 0;
            }

        } else {
            epd->websocket->sended += n;
        }
    }

    if(epd->websocket->sended >= epd->websocket->data_len) {
        serv_status.sending_counts--;
        epd->status = STEP_WAIT;
        epd->websocket->data = NULL;
        epd->websocket->data_len = -1;
        //printf ( "send finish %d\n", epd->websocket->data_len );
        se_be_read(epd->se_ptr, websocket_be_read);
        lua_State *L = (lua_State *) epd->L;
        lua_pushboolean(L, 1);
        lua_resume(L, 1);
        return 0;
    }

    return 1;
}

int ws_send_data(epdata_t *epd, unsigned int fin, unsigned int rsv1, unsigned int rsv2, unsigned int rsv3,
                 unsigned int opcode, uint64_t payload_len, const char *payload_data)
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

    bzero(&buf_256, 256);
    buf_256[0] |= ((fin << 7) | (rsv1 << 6) | (rsv2 << 5) | (rsv3 << 4) | opcode);

    if(payload_len < 126) {
        buf_256[1] |= ((frame_mask << 7) | payload_len);
        offset = 2;

    } else if(payload_len >= 126 && payload_len <= 0xFFFF) {
        void *p = &buf_256[2];
        buf_256[1] = 126;
        * (uint16_t *) p = htons((uint16_t) payload_len);
        offset = 4;

    } else {
        void *p = &buf_256[2];
        buf_256[1] = 127;
        * (uint64_t *) p = ntohll((uint64_t) payload_len);
        offset = 10;
    }

    update_timeout(epd->timeout_ptr, STEP_WS_SEND_TIMEOUT);

    if(offset + payload_len < 256) {
        memcpy(buf_256 + offset, payload_data, payload_len);

        n = network_raw_send(epd->fd, buf_256, offset + payload_len);

        if(n <= 0) {
            return -1;
        }

        return n;

    } else {
        n = network_raw_send(epd->fd, buf_256, offset);

        if(n <= 1) {
            return -1;
        }

        epd->websocket->data = (char *) payload_data;
        epd->websocket->data_len = payload_len;
        //printf ( "bewrite %d\n", epd->websocket->data_len );
        epd->websocket->sended = 0; // for sended count
        epd->status = STEP_SEND;
        serv_status.sending_counts++;
        se_be_write(epd->se_ptr, websocket_be_write);

        return 1;
    }
}


int lua_f_is_websocket(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    if(!epd) {
        return 0;
    }

    lua_pushboolean(L, epd->websocket ? 1 : 0);
    return 1;
}
/*
$ sysctl net.inet.tcp.always_keepalive
net.inet.tcp.always_keepalive: 0
$ sudo sysctl -w net.inet.tcp.always_keepalive=1
net.inet.tcp.always_keepalive: 0 -> 1
*/
int lua_f_upgrade_to_websocket(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    if(!epd) {
        return 0;
    }

    if(epd->websocket) {
        return 0;
    }

    epd->keepalive = 1;
    epd->websocket = malloc(sizeof(websocket_pt));

    if(!epd->websocket) {
        lua_pushboolean(L, 0);
        return 1;
    }

    network_be_end(epd);
    //lua_pushnil ( L );
    epd->websocket->data = NULL;
    epd->websocket->is_multi_frame = 0;

    epd->websocket->ended = 0;

    epd->status = STEP_WAIT;

    lua_pushvalue(L, 1);
    lua_setglobal(L, "__websocket_on__");

    return 0;
}

int lua_f_check_websocket_close(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    if(!epd) {
        return 0;
    }

    if(epd->websocket->ended) {
        lua_pushboolean(L, 1);
        return 1;
    }

    if(check_process_for_exit()) {
        ws_send_data(epd, 1, 0, 0, 0, WS_OPCODE_CLOSE, 0, NULL);

        se_delete(epd->se_ptr);
        epd->se_ptr = NULL;
        delete_timeout(epd->timeout_ptr);
        epd->timeout_ptr = NULL;

        if(epd->fd > -1) {
            serv_status.active_counts--;
            close(epd->fd);
            epd->fd = -1;
        }

        lua_pushboolean(L, 1);
        return 1;
    }

    return _lua_sleep(L, 10);
}

int lua_f_websocket_send(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    if(!epd) {
        lua_pushnil(L);
        lua_error(L);
        return 0;
    }

    if(epd->websocket->ended) {
        return 0;
    }

    if(!epd->websocket) {
        luaL_error(L, "not a websocket!");
        return 0;
    }

    if(epd->websocket->data) {
        lua_pushnil(L);
        lua_pushstring(L, "websocket busy!");
        return 2;
    }

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "miss content!");
        return 0;
    }

    size_t len;
    const char *data = lua_tolstring(L, 1, &len);

    int r = 0;

    if(lua_gettop(L) < 2 && epd->websocket->is_multi_frame == 1) {       // mid frames
        r = ws_send_data(epd,  0, 0, 0, 0, WS_OPCODE_CONTINUE, len, data);

    } else {
        if(lua_isboolean(L, 3)) {       // multi frames
            if(lua_toboolean(L, 3) == 0) {       // first frame
                r = ws_send_data(epd,  0, 0, 0, 0, (lua_isboolean(L, 2)
                                                    && lua_toboolean(L, 2)) ? WS_OPCODE_BINARY : WS_OPCODE_TEXT, len, data);
                epd->websocket->is_multi_frame = 1;

            } else { // last frame
                r = ws_send_data(epd,  1, 0, 0, 0, WS_OPCODE_CONTINUE, len, data);
                epd->websocket->is_multi_frame = 0;
            }

        } else { // single frame
            r = ws_send_data(epd,  1, 0, 0, 0, (lua_isboolean(L, 2)
                                                && lua_toboolean(L, 2)) ? WS_OPCODE_BINARY : WS_OPCODE_TEXT, len, data);
        }
    }

    if(r == -1) {
        lua_pushnil(L);
        lua_pushstring(L, "send error!");
        return 2;
    }

    if(r == 1) {
        return lua_yield(L, 0);
    }

    lua_pushboolean(L, 1);
    return 1;
}

