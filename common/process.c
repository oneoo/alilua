#include "process.h"

#ifdef linux
#define app_panic(format, args...) \
    do {    \
        printf(format, ## args);    \
        abort();    \
    } while(0)

static inline void print_cpu_mask ( cpu_set_t cpu_mask )
{
    uint8_t flag;
    uint32_t i;
    printf ( "%d Cpu affinity is ", getpid() );
    flag = 0;

    for ( i = 0; i < sizeof ( cpu_set_t ); i ++ ) {
        if ( CPU_ISSET ( i, &cpu_mask ) ) {
            if ( flag == 0 ) {
                flag = 1;
                printf ( "%d", i );

            } else {
                printf ( ",%d", i );
            }
        }
    }

    printf ( ".\n" );
}

static inline void get_cpu_mask ( pid_t pid, cpu_set_t *mask )
{
    if ( sched_getaffinity ( pid, sizeof ( cpu_set_t ), mask ) == -1 ) {
        //app_panic("Get cpu affinity failed.\n");
    }
}

static inline void set_cpu_mask ( pid_t pid, cpu_set_t *mask )
{
    if ( sched_setaffinity ( pid, sizeof ( cpu_set_t ), mask ) == -1 ) {
        //app_panic("Set cpu affinity failed.\n");
    }
}
#endif

void active_cpu ( uint32_t active_cpu )
{
#ifdef linux
    cpu_set_t cpu_mask;
    int cpu_count = sysconf ( _SC_NPROCESSORS_ONLN );

    if ( cpu_count < 1 ) {
        return;
    }

    active_cpu = active_cpu % cpu_count;

    get_cpu_mask ( 0, &cpu_mask );
    //print_cpu_mask(cpu_mask);

    CPU_ZERO ( &cpu_mask );
    CPU_SET ( active_cpu, &cpu_mask );
    set_cpu_mask ( 0, &cpu_mask );

    //get_cpu_mask(0, &cpu_mask);
    //print_cpu_mask(cpu_mask);
#endif
}

void setProcTitle ( const char *title, int is_master )
{
    _argv[1] = 0;
    char *p = _argv[0];
    memset ( p, 0x00, process_char_last - p );

    if ( is_master ) {
        snprintf ( p, process_char_last - p, "%s: %s %s", process_name, title, process_chdir );
        int i = 1;

        for ( i = 1; i < _argc; i++ ) {
            sprintf ( p, "%s %s", p, environ[i] );
        }

    } else {
        snprintf ( p, process_char_last - p, "%s: %s", process_name, title );
    }
}

char *initProcTitle ( int argc, char **argv )
{
    _argc = argc;
    _argv = argv;
    size_t n = 0;
    int i = 0;
#ifdef linux
    n = readlink ( "/proc/self/exe" , process_chdir , sizeof ( process_chdir ) );
#else
    uint32_t new_argv0_s = sizeof ( process_chdir );

    if ( _NSGetExecutablePath ( process_chdir, &new_argv0_s ) == 0 ) {
        n = strlen ( process_chdir );
    }

#endif
    i = n;

    while ( n > 1 ) if ( process_chdir[n--] == '/' ) {
            strncpy ( process_name, ( ( char * ) process_chdir ) + n + 2, i - n );
            process_chdir[n + 1] = '\0';
            break;
        }

    chdir ( process_chdir );

    n = 0;

    for ( i = 0; argv[i]; ++i ) {
        n += strlen ( argv[i] ) + 1;
    }

    char *raw = malloc ( n );

    for ( i = 0; argv[i]; ++i ) {
        memcpy ( raw, argv[i], strlen ( argv[i] ) + 1 );
        environ[i] = raw;
        raw += strlen ( environ[i] ) + 1;
    }

    process_char_last = argv[0];

    for ( i = 0; i < argc; ++i ) {
        process_char_last += strlen ( argv[i] ) + 1;
    }

    for ( i = 0; environ[i]; ++i ) {
        process_char_last += strlen ( environ[i] ) + 1;
    }

    return process_chdir;
}

static int _signal_handler = 0;
int checkProcessForExit()
{
    return _signal_handler == 1;
}

void signal_handler ( int sig )
{
    switch ( sig ) {
        case SIGHUP:
            break;

        case SIGTERM:
            _signal_handler = 1;
            break;
    }
}

void daemonize()
{
    int i, lfp;
    char str[10];

    if ( getppid() == 1 ) {
        return;
    }

    i = fork();

    if ( i < 0 ) {
        exit ( 1 );
    }

    if ( i > 0 ) {
        exit ( 0 );
    }

    setsid();
    signal ( SIGCHLD, SIG_IGN );
    signal ( SIGTSTP, SIG_IGN );
    signal ( SIGTTOU, SIG_IGN );
    signal ( SIGTTIN, SIG_IGN );
    signal ( SIGHUP, signal_handler );
    signal ( SIGTERM, signal_handler );
}

void attach_on_exit ( void *fun )
{
    struct sigaction myAction;
    myAction.sa_handler = fun;
    sigemptyset ( &myAction.sa_mask );
    myAction.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction ( SIGSEGV, &myAction, NULL );
    sigaction ( SIGUSR1, &myAction, NULL );
    sigaction ( SIGFPE, &myAction, NULL );
    sigaction ( SIGILL, &myAction, NULL );
    sigaction ( SIGBUS, &myAction, NULL );
    sigaction ( SIGABRT, &myAction, NULL );
    sigaction ( SIGSYS, &myAction, NULL );
    sigaction ( SIGQUIT, &myAction, NULL );
    sigaction ( SIGTRAP, &myAction, NULL );
    sigaction ( SIGXCPU, &myAction, NULL );
    sigaction ( SIGXFSZ, &myAction, NULL );

    sigaction ( SIGINT, &myAction, NULL ); /// CTRL+C
}

static int _workerprocess[200];
static int _workerprocess_count = 0;
static void ( *_workerprocess_func[200] ) ();
int forkProcess ( void ( *func ) () )
{
    int ret = fork();

    if ( ret == 0 ) {
        active_cpu ( _workerprocess_count );
        func ( _workerprocess_count );
    }

    if ( ret > 0 ) {
        _workerprocess[_workerprocess_count] = ret;
        _workerprocess_func[_workerprocess_count] = func;
        _workerprocess_count++;
    }

    return ret;
}

void safeProcess()
{
    int i;

    for ( i = 0; i < _workerprocess_count; i++ ) {
        if ( waitpid ( _workerprocess[i], NULL, WNOHANG ) == -1 ) {
            int ret = fork();

            if ( ret == 0 ) {
                active_cpu ( i );
                _workerprocess_func[i] ( i );
            }

            _workerprocess[i] = ret;
        }
    }
}

void waitForChildProcessExit()
{
    int i = 0, k = 0;

    while ( k != _workerprocess_count ) {
        for ( i = 0; i < _workerprocess_count; i++ ) {
            if ( waitpid ( _workerprocess[i], NULL, WNOHANG ) == -1 ) {
                k++;
            }
        }

        sleep ( 1 );
    }
}

void setProcessUser ( char *user, char *group )
{
    struct passwd *pw;
    char *username = "nobody";

    if ( user ) {
        username = user;
    }

    char *groupname = "nobody";

    if ( group ) {
        groupname = group;
    }

    pw = getpwnam ( username );

    if ( !pw ) {
        printf ( "user %s is not exist\n", username );
        exit ( 1 );
    }

    if ( setuid ( pw->pw_uid ) ) {
        setgid ( pw->pw_gid );
        return;
    }

    struct group *grp = getgrnam ( groupname );

    if ( grp ) {
        setgid ( grp->gr_gid );

    } else {
        char *groupname2 = "nogroup";
        grp = getgrnam ( groupname );

        if ( grp ) {
            setgid ( grp->gr_gid );
        }
    }
}

char *getarg ( char *key )
{
    int i = 0;
    int addp;

    while ( i < sizeof ( environ ) && environ[i] != NULL) {
        addp = environ[i][0] == '-'
               && environ[i][1] == '-' ? 2 : ( environ[i][0] == '-' ? 1 : 0 );

        if ( strncmp ( environ[i] + addp, key, strlen ( key ) ) == 0
             && ( environ[i][strlen ( key ) + addp] == '='
                  || environ[i][strlen ( key ) + addp] == '\0' ) ) {
            return environ[i] + strlen ( key ) + addp + 1;
        }

        i++;
    }

    return NULL;
}

int new_thread ( void *func )
{
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init ( &attr );
    pthread_attr_setscope ( &attr, PTHREAD_SCOPE_SYSTEM );

    pthread_attr_setdetachstate ( &attr, PTHREAD_CREATE_DETACHED );

    int i = 0;

    if ( pthread_create ( &thread, &attr, func, &i ) ) {
        return 0;
    }

    return 1;
}

int new_thread_i ( void *func, void *i )
{
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init ( &attr );
    pthread_attr_setscope ( &attr, PTHREAD_SCOPE_SYSTEM );

    pthread_attr_setdetachstate ( &attr, PTHREAD_CREATE_DETACHED );

    if ( pthread_create ( &thread, &attr, func, i ) ) {
        return 0;
    }

    return 1;
}

shm_t *shm_malloc ( size_t size )
{
    int oflag, sem_id, shm_id, i;
    union semun arg;

    /* create and init a shared memory segment for the counter */
    oflag = 0666 | IPC_CREAT;

    if ( ( shm_id = shmget ( ftok ( environ[0], shm_ftok_id ), size, oflag ) ) < 0 ) {
        perror ( "shmget" );
        return NULL;
    }

    void *p = NULL;

    if ( ( p = shmat ( shm_id, NULL, 0 ) ) < 0 ) {
        perror ( "shmat" );
        return NULL;
    }

    /* create and init the semaphore that will protect the counter */
    oflag = 0666 | IPC_CREAT;

    if ( ( sem_id = semget ( ftok ( environ[0], shm_ftok_id ), 1, oflag ) ) < 0 ) {
        perror ( "semget1" );
        return NULL;
    }

    arg.val = 1; /* binary semaphore */

    if ( semctl ( sem_id, 0, SETVAL, arg ) < 0 ) {
        perror ( "semctl" );
        return NULL;
    }

    shm_t *o = malloc ( sizeof ( shm_t ) );
    o->shm_id = shm_id;
    o->sem_id = sem_id;
    o->p = p;

    shm_ftok_id++;

    return o;
}

void shm_free ( shm_t *shm )
{
    if ( shm == NULL ) {
        return;
    }

    union semun arg;

    arg.val = 1; /* binary semaphore */

    /* remove the semaphore and shm segment */
    if ( semctl ( shm->sem_id, 0, IPC_RMID, arg ) < 0 ) {
        return;
    }

    if ( shmctl ( shm->shm_id, IPC_RMID, NULL ) < 0 ) {
        perror ( "shmctl-rm" );
        return;
    }

    free ( shm );
    shm = NULL;
}

/* P - decrement semaphore and wait */
int shm_lock ( shm_t *shm )
{
    static struct sembuf sembuf;

    sembuf.sem_num = 0;
    sembuf.sem_op = -1;
    sembuf.sem_flg = 0;
    return semop ( shm->sem_id, &sembuf, 1 );
}

/* V - increment semaphore and signal */
int shm_unlock ( shm_t *shm )
{
    static struct sembuf sembuf;

    sembuf.sem_num = 0;
    sembuf.sem_op =  1;
    sembuf.sem_flg = 0;
    return semop ( shm->sem_id, &sembuf, 1 );
}
