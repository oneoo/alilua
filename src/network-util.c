#include <zlib.h>
#include <sys/uio.h>

#include "main.h"
#include "config.h"
#include "worker.h"
#include "network.h"

static char temp_buf[8192];
static serv_status_t tmp_status;
static time_t last_sync = 0;

void network_send_error(epdata_t *epd, int code, const char *msg)
{
    char *code_ = NULL;
    int len = strlen(msg);

    if(code < 300) {
        if(code == 100) {
            code_ = "Continue";

        } else if(code == 101) {
            code_ = "Switching Protocols";

        } else if(code == 200) {
            code_ = "OK";

        } else if(code == 201) {
            code_ = "Created";

        } else if(code == 202) {
            code_ = "Accepted";

        } else if(code == 203) {
            code_ = "Non-Authoritative Information";

        } else if(code == 204) {
            code_ = "No Content";

        } else if(code == 205) {
            code_ = "Reset Content";

        } else if(code == 206) {
            code_ = "Partial Content";
        }

    } else if(code < 400) {
        if(code == 300) {
            code_ = "Multiple Choices";

        } else if(code == 301) {
            code_ = "Moved Permanently";

        } else if(code == 302) {
            code_ = "Found";

        } else if(code == 303) {
            code_ = "See Other";

        } else if(code == 304) {
            code_ = "Not Modified";

        } else if(code == 305) {
            code_ = "Use Proxy";

        } else if(code == 307) {
            code_ = "Temporary Redirect";
        }

    } else if(code < 500) {
        if(code == 400) {
            code_ = "Bad Request";

        } else if(code == 401) {
            code_ = "Unauthorized";

        } else if(code == 402) {
            code_ = "Payment Required";

        } else if(code == 403) {
            code_ = "Forbidden";

        } else if(code == 404) {
            code_ = "Not Found";

        } else if(code == 405) {
            code_ = "Method Not Allowed";

        } else if(code == 406) {
            code_ = "Not Acceptable";

        } else if(code == 407) {
            code_ = "Proxy Authentication Required";

        } else if(code == 408) {
            code_ = "Request Timeout";

        } else if(code == 409) {
            code_ = "Conflict";

        } else if(code == 410) {
            code_ = "Gone";

        } else if(code == 411) {
            code_ = "Length Required";

        } else if(code == 412) {
            code_ = "Precondition Failed";

        } else if(code == 413) {
            code_ = "Request Entity Too Large";

        } else if(code == 414) {
            code_ = "Request-URI Too Long";

        } else if(code == 415) {
            code_ = "Unsupported Media Type";

        } else if(code == 416) {
            code_ = "Requested Range Not Satisfiable";

        } else if(code == 417) {
            code_ = "Expectation Failed";
        }

    } else {
        if(code == 500) {
            code_ = "Internal Server Error";

        } else if(code == 501) {
            code_ = "Not Implemented";

        } else if(code == 502) {
            code_ = "Bad Gateway";

        } else if(code == 503) {
            code_ = "Service Unavailable";

        } else if(code == 504) {
            code_ = "Gateway Timeout";

        } else if(code == 505) {
            code_ = "HTTP Version Not Supported";
        }
    }

    //sprintf(temp_buf,"HTTP/1.1 %d %s\r\nServer: (%s)\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %d\r\n\r\n<h1>%d %s</h1><p>%s</p>", code, code_, hostname, (int)(strlen(msg)+20+strlen(code_)), code, code_, msg);
    int len2 = sprintf(temp_buf, "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=UTF-8", code, code_, hostname);
    temp_buf[len2] = '\0';

    int e = epd->process_timeout;
    epd->process_timeout = 0;

    if(code == 408 && epd->iov[0].iov_base) {    /// clear lua output
        epd->iov[0].iov_len = 0;
    }

    network_send_header(epd, temp_buf);
    network_send(epd, msg, len);
    epd->process_timeout = e;
    network_be_end(epd);
}

void sync_serv_status()
{
    if(now <= last_sync) {
        return;
    }

    last_sync = now;

    serv_status.sec_process_counts[(now - 5) % 5] = 0;

    int i, k = 0;

    shm_lock(_shm_serv_status);
    shm_serv_status->connect_counts += serv_status.connect_counts;
    serv_status.connect_counts = 0;
    shm_serv_status->success_counts += serv_status.success_counts;
    serv_status.success_counts = 0;
    shm_serv_status->process_counts += serv_status.process_counts;
    serv_status.process_counts = 0;

    shm_serv_status->waiting_counts += serv_status.waiting_counts - tmp_status.waiting_counts;
    shm_serv_status->reading_counts += serv_status.reading_counts - tmp_status.reading_counts;
    shm_serv_status->sending_counts += serv_status.sending_counts - tmp_status.sending_counts;

    shm_serv_status->active_counts += serv_status.active_counts - tmp_status.active_counts;

    shm_serv_status->sec_process_counts[0] += serv_status.sec_process_counts[0] - tmp_status.sec_process_counts[0];
    shm_serv_status->sec_process_counts[1] += serv_status.sec_process_counts[1] - tmp_status.sec_process_counts[1];
    shm_serv_status->sec_process_counts[2] += serv_status.sec_process_counts[2] - tmp_status.sec_process_counts[2];
    shm_serv_status->sec_process_counts[3] += serv_status.sec_process_counts[3] - tmp_status.sec_process_counts[3];
    shm_serv_status->sec_process_counts[4] += serv_status.sec_process_counts[4] - tmp_status.sec_process_counts[4];

    memcpy(&tmp_status, &serv_status, sizeof(serv_status_t));

    shm_unlock(_shm_serv_status);
}

void network_send_status(epdata_t *epd)
{
    int len = sprintf(temp_buf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8", hostname);

    network_send_header(epd, temp_buf);

    shm_lock(_shm_serv_status);
    int sum_cs = 0;
    int i = 0;

    for(i = now - 3; i < now; i++) {
        sum_cs += shm_serv_status->sec_process_counts[i % 5];
    }

    len = sprintf(temp_buf, "Active connections: %d\nserver accepts handled requests\n%"
                  PRIu64 " %" PRIu64 " %" PRIu64
                  "\nReading: %d Writing: %d Waiting: %d\nRequests per second: %d [#/sec] (mean)\n",
                  shm_serv_status->active_counts,
                  shm_serv_status->connect_counts, shm_serv_status->success_counts,
                  shm_serv_status->process_counts,
                  shm_serv_status->reading_counts, shm_serv_status->sending_counts,
                  shm_serv_status->active_counts - shm_serv_status->reading_counts -
                  shm_serv_status->sending_counts, (sum_cs / 3));

    shm_unlock(_shm_serv_status);

    network_send(epd, temp_buf, len);
    network_be_end(epd);
}

int gzip_iov(int mode, struct iovec *iov, int iov_count, int *_diov_count)
{
    //char buf[EP_D_BUF_SIZE];
    char *buf = temp_buf;
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;
    int ret = deflateInit2(&stream,
                           GZIP_LEVEL, // gzip level
                           Z_DEFLATED,
                           -MAX_WBITS,
                           8,
                           Z_DEFAULT_STRATEGY);

    if(Z_OK != ret) {
        LOGF(ERR, "deflateInit error: %d\r\n", ret);
        return 0;
    }

    /// iov[0] is header block , do not gzip!!!
    int i = 1, dlen = 0, ilen = 0, diov_count = 1, zed = 0, cpd = 0;
    unsigned crc = 0L;

    int flush = 0;

    do {
        if(i >= _MAX_IOV_COUNT || !iov[i].iov_base) {
            break;
        }

        ilen += iov[i].iov_len;

        if(mode == 1) {    /// deflate mode , not need crc
            crc = crc32(crc, iov[i].iov_base, iov[i].iov_len);
        }

        stream.next_in = iov[i].iov_base;
        stream.avail_in = iov[i].iov_len;
        iov[i].iov_len = 0;

        flush = (i == iov_count || i + 1 >= _MAX_IOV_COUNT || iov[i + 1].iov_base == NULL) ? Z_FINISH : Z_NO_FLUSH;
        i++;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            stream.avail_out = EP_D_BUF_SIZE;
            stream.next_out = buf;
            ret = deflate(&stream, flush);    /* no bad return value */

            if(ret == Z_STREAM_ERROR) {
                deflateEnd(&stream);
                LOGF(ERR, "Z_STREAM_ERROR | Z_MEM_ERROR\n");
                return 0;
            }

            zed = EP_D_BUF_SIZE - stream.avail_out;

            if(zed > 0) {
                /// send at header block !!!!! not here
                /*if(diov_count == 1){
                    memcpy(iov[diov_count].iov_base, gzip_header, 10);
                    iov[diov_count].iov_len = 10;
                    dlen += 10;
                }*/
                dlen += zed;

                if(zed + iov[diov_count].iov_len <= EP_D_BUF_SIZE) {
                    memcpy(iov[diov_count].iov_base + iov[diov_count].iov_len, buf, zed);
                    iov[diov_count].iov_len += zed;

                    if(iov[diov_count].iov_len == EP_D_BUF_SIZE) {
                        diov_count++;

                        if(iov[diov_count].iov_base == NULL) {
                            deflateEnd(&stream);
                            LOGF(ERR,  "gzip: iov buf count error!\n");
                            return 0;
                        }
                    }

                } else {
                    cpd = EP_D_BUF_SIZE - iov[diov_count].iov_len;

                    if(cpd > 0) {
                        memcpy(iov[diov_count].iov_base + iov[diov_count].iov_len, buf, cpd);
                        iov[diov_count].iov_len += cpd;

                        if(zed == cpd) {
                            continue;
                        }
                    }

                    if(diov_count + 1 < i && iov[diov_count + 1].iov_base != NULL) {
                        diov_count++;
                        memcpy(iov[diov_count].iov_base, buf + cpd, zed - cpd);
                        iov[diov_count].iov_len = zed - cpd;

                    } else {
                        deflateEnd(&stream);
                        LOGF(ERR,  "gzip: iov buf count error!\n");
                        return 0;
                    }
                }
            }
        } while(stream.avail_out == 0);

        /* done when last data in file processed */
    } while(flush != Z_FINISH);

    /* clean up and return */
    deflateEnd(&stream);

    if(mode == 1) {
        char *o = ((char *) iov[diov_count].iov_base);
        o[iov[diov_count].iov_len++] = (crc & 0xff);
        o[iov[diov_count].iov_len++] = ((crc >> 8) & 0xff);
        o[iov[diov_count].iov_len++] = ((crc >> 16) & 0xff);
        o[iov[diov_count].iov_len++] = ((crc >> 24) & 0xff);

        o[iov[diov_count].iov_len++] = (ilen & 0xff);
        o[iov[diov_count].iov_len++] = ((ilen >> 8) & 0xff);
        o[iov[diov_count].iov_len++] = ((ilen >> 16) & 0xff);
        o[iov[diov_count].iov_len++] = ((ilen >> 24) & 0xff);

        dlen += 8;
    }

    int j = diov_count + 1;

    for(; j < i; j++) {
        free(iov[j].iov_base);
        iov[j].iov_base = NULL;
        iov[j].iov_len = 0;
    }

    *_diov_count = diov_count;

    return dlen;
}

