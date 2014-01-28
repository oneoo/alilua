#include "network.h"

#ifndef _ALILUA_WORKER
#define _ALILUA_WORKER

void worker_main(int _worker_n);
void free_epd(epdata_t *epd);
void close_client(epdata_t *epd);

#endif