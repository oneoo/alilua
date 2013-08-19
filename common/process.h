#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <inttypes.h>
#include <sched.h>

#ifndef PROCESS_H
#define PROCESS_H
extern char **environ;
char *process_char_last;
char process_chdir[924];
char process_name[100];

char **_argv;
int _argc;

void active_cpu ( uint32_t active_cpu );
void setProcTitle ( const char *title, int is_master );
char *initProcTitle ( int argc, char **argv );
int checkProcessForExit();
void signal_handler ( int sig );
void daemonize();
void setProcessUser ( char *user, char *group );
char *getarg ( char *key );
int forkProcess ( void ( *func ) ( int i ) );
void safeProcess();
void waitForChildProcessExit();
#ifdef linux
#ifndef _SEMUN_H
#define _SEMUN_H
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short int *array;
    struct seminfo *__buf;
};
#endif /// linux
# endif
static int shm_ftok_id = 0;
typedef struct _shm_t {
    int shm_id;
    int sem_id;
    void *p;
} shm_t;
shm_t *shm_malloc ( size_t size );
void shm_free ( shm_t *shm );
int shm_lock ( shm_t *shm );
int shm_unlock ( shm_t *shm );
#endif
