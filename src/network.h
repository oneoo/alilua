#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>
#ifdef linux
#include <sys/sendfile.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <zlib.h>

#include "config.h"
#include "../common/process.h"
#include "../se/se.h"

#define free(p) do { if (p) { free(p); p = NULL; } } while (0)
#define close(fd) do { if (fd >= 0) { close(fd); fd = -1; } } while (0)
//printf("closeat %s:%d\n",__FILE__,__LINE__);

#ifndef _NETWORK_H
#define _NETWORK_H

#define STEP_READ 1
#define STEP_JOIN_PROCESS 2
#define STEP_PROCESS 3
#define STEP_SEND 4
#define STEP_FINISH 5
#define STEP_WAIT 6
#define STEP_ASYNC 7
#define STEP_ERROR 8

shm_t *_shm_serv_status;

typedef struct {
    uint8_t masking_key_offset;
    uint8_t frame_mask;
    unsigned char frame_masking_key[4];
    int websocket_handles;
    void *ML;
    void *L;
    void *data;
    uint32_t data_len;
    uint32_t sended;
} websocket_pt;

typedef struct _epdata_t {
    void *se_ptr;
    void *timeout_ptr;
    uint8_t status;
    websocket_pt *websocket;

    int fd;
    unsigned char *headers;
    int header_len;
    unsigned char *contents;
    int content_length;

    char *method;
    char *uri;
    char *host;
    char *query;
    char *http_ver;
    char *referer;
    char *user_agent;
    long  start_time;

    int data_len;
    int buf_size;
    int _header_length;

    int keepalive;
    int process_timeout;

    time_t stime;

    int response_sendfile_fd;

#define _MAX_IOV_COUNT 242
    struct iovec iov[_MAX_IOV_COUNT];
    int response_header_length;
    int iov_buf_count;
    int response_content_length;
    off_t response_buf_sended;


    struct _epdata_t *job_next;
    struct _epdata_t *job_uper;
    struct in_addr client_addr;
    char z[4]; /// align size to 4096
} epdata_t;

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

void errorlog ( epdata_t *epd, const char *msg );
char *url_encode ( char const *s, int raw, size_t len, size_t *new_length );
char *url_decode ( char *str, int raw, size_t *new_length );
char *stristr ( const char *str, const char *pat, int length );
int network_bind ( char *addr, int port );
int network_raw_send ( int client_fd, const char *contents, int length );
int network_raw_sendfile ( int out_fd, int in_fd, off_t *offset, size_t count );
char *network_raw_read ( int cfd, int *datas_len );
int network_connect ( const char *ser, int port );
void network_send_error ( epdata_t *epd, int code, const char *msg );
void network_send_status ( epdata_t *epd );
static void network_end_process ( epdata_t *epd );
int setnonblocking ( int fd );
void sync_serv_status();

static int network_be_read ( se_ptr_t *ptr );
static int network_be_write ( se_ptr_t *ptr );

int ws_send_data ( epdata_t *epd,
                   unsigned int fin,
                   unsigned int rsv1,
                   unsigned int rsv2,
                   unsigned int rsv3,
                   unsigned int opcode,
                   uint64_t payload_len,
                   const char *payload_data );
int websocket_be_read ( se_ptr_t *ptr );
int websocket_be_write ( se_ptr_t *ptr );

int network_send_header ( epdata_t *epd, const char *header );
int network_send ( epdata_t *epd, const char *data, int _len );
int network_sendfile ( epdata_t *epd, const char *path );
void network_be_end ( epdata_t *epd );

static int is_daemon = 0;

static int loop_fd = -1;
static int has_error_for_exit = 0;

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

static const char *DAYS_OF_WEEK[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTHS_OF_YEAR[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
time_t now;
static struct tm now_tm;
char now_date[100];

static void free_epd ( epdata_t *epd );
static void free_epd_request ( epdata_t *epd );
void close_client ( epdata_t *epd );
static int ( *process_func ) ( epdata_t *epd, int thread_at );
void network_worker ( void *_process_func, int work_thread_count, int process_at );

static const char gzip_header[10] = {'\037', '\213', Z_DEFLATED, 0, 0, 0, 0, 0, 0, 0x03};
int gzip_iov ( int mode, struct iovec *iov, int iov_count, int *_diov_count );

void init_mime_types();
const char *get_mime_type ( const char *filename );
#endif /// _NETWORK_H
