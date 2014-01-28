
#ifndef _ALILUA_WEBSOCKET_H
#define _ALILUA_WEBSOCKET_H


#define WS_FRAME_FIN_AND_RSV        0x80

#define WS_FRAME_OPCODE_CONTINUATION    0
#define WS_FRAME_OPCODE_TEXT        1
#define WS_FRAME_OPCODE_BINARY      2
#define WS_FRAME_OPCODE_CLOSE       8
#define WS_FRAME_OPCODE_PING        9
#define WS_FRAME_OPCODE_PONG        10

#define WS_FRAME_MASK           0
#define WS_OPCODE_CONTINUE   0x00
#define WS_OPCODE_TEXT       0x01
#define WS_OPCODE_BINARY     0x02

/* opcode: control frames */
#define WS_OPCODE_CLOSE      0x08
#define WS_OPCODE_PING       0x09
#define WS_OPCODE_PONG       0x0a

int websocket_be_read(se_ptr_t *ptr);
int websocket_be_write(se_ptr_t *ptr);
int ws_send_data(epdata_t *epd, unsigned int fin, unsigned int rsv1, unsigned int rsv2, unsigned int rsv3,
                 unsigned int opcode, uint64_t payload_len, const char *payload_data);

int lua_f_is_websocket(lua_State *L);
int lua_f_upgrade_to_websocket(lua_State *L);
int lua_f_websocket_send(lua_State *L);

#endif