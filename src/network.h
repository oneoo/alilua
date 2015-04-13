#include "main.h"

#ifndef _ALILUA_NETWORK_H
#define _ALILUA_NETWORK_H

typedef struct {
    uint8_t masking_key_offset;
    uint8_t frame_mask;
    uint8_t is_multi_frame;
    unsigned char frame_masking_key[4];
    int ended;
    void *L;
    void *data;
    uint32_t data_len;
    uint32_t sended;
} websocket_pt;

typedef struct _epdata_t {
    void *se_ptr;
    void *timeout_ptr;
    uint8_t status;
    uint8_t header_sended;
    uint8_t has_content_length_or_chunk_out;
    uint8_t content_gzip_or_deflated;
    unsigned zs_crc;
    z_stream *zs;
    unsigned long total_response_content_length;
    websocket_pt *websocket;
    lua_State *L;
    char *vhost_root;
    int vhost_root_len;

    int fd;
    unsigned char *headers;
    int header_len;
    int content_length;
    unsigned char *contents;

    char *method;
    char *uri;
    char *host;
    char *query;
    char *http_ver;
    char *referer;
    char *user_agent;
    char *if_modified_since;
    char *boundary;
    long start_time;

    int data_len;
    int buf_size;
    int _header_length;

    int keepalive;
    int process_timeout;

    int response_sendfile_fd;

#define _MAX_IOV_COUNT 238
    struct iovec iov[_MAX_IOV_COUNT];
    int response_header_length;
    int iov_buf_count;
    int response_content_length;
    off_t response_buf_sended;

    struct _epdata_t *job_next;
    struct _epdata_t *job_uper;
    struct in_addr client_addr;
    struct in_addr server_addr;

    SSL *ssl;

    se_rw_proc_t next_proc;
    char *next_out;
    int next_out_len;

    int ssl_verify;

    char z[4]; /// align size to 4096
} epdata_t;

typedef struct {
    uint64_t connect_counts;
    uint64_t success_counts;
    uint64_t process_counts;
    int waiting_counts;
    int reading_counts;
    int sending_counts;
    int jioning_counts;
    int active_counts;

    int sec_process_counts[5];
    time_t ptime;
} serv_status_t;

serv_status_t serv_status;
serv_status_t *shm_serv_status;

int network_be_read(se_ptr_t *ptr);
int network_be_write(se_ptr_t *ptr);

int network_send_header(epdata_t *epd, const char *header);
int network_send(epdata_t *epd, const char *data, int _len);
int network_sendfile(epdata_t *epd, const char *path);
void network_be_end(epdata_t *epd);
int network_flush(epdata_t *epd);

/* network-util.c */
void network_send_error(epdata_t *epd, int code, const char *msg);
void sync_serv_status();
void network_send_status(epdata_t *epd);
int gzip_iov(epdata_t *epd, int is_flush, struct iovec *iov, int iov_count, int *_diov_count);

void init_lua_threads(lua_State *_L, int count);
lua_State *new_lua_thread(lua_State *_L);
void reinit_lua_thread_env(lua_State *L);
void release_lua_thread(lua_State *L);

#endif /// _ALILUA_NETWORK_H
