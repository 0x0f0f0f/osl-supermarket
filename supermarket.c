#include <stdlib.h> 
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "config.h"
#include "util.h"
#include "conc_lqueue.h"

#define MAX_CONN_ATTEMPTS 10
#define CONN_ATTEMPT_DELAY 1

typedef struct signal_worker_opt_s {
    // Signal set to be handled
    sigset_t sigset;
} signal_worker_opt_t;

// This thread waits for SIGHUP to perform a gentle exit
void* signal_worker(void* arg) {
    signal_worker_opt_t opt = *(signal_worker_opt_t *) arg;
    int err, signum;
    while(1) {
        SYSCALL(err, sigwait(&opt.sigset, &signum), "waiting for signals\n");
        LOG_DEBUG("Intercepted Signal %s\n", strsignal(signum));
        // TODO if (signum == SIGHUP) signal other threads to quit gently;
        // else if (signum == SIGQUIT) exit brutally (?)
        exit(0);
   }
   exit(EXIT_FAILURE);
}

// Contains options passed to the messaging thread
typedef struct msg_opt_s {
    conc_lqueue_t msgqueue; 
} msg_opt_t;


void* message_worker(void* arg) {
    int sock_fd, attempt_count = 0, sock_flags = 0, err = 0;
    struct sockaddr_un addr;
    struct sigaction act; 

    // Ignore SIGHUP
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    SYSCALL(err, sigaction(SIGHUP, &act, NULL), "registering handler\n");

    // Prepare socket addr
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);
    unlink(DEFAULT_SOCK_PATH);
    
    SYSCALL(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0), "creating socket\n");
    while(connect(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        LOG_CRITICAL("Error connecting to server: %s\n", strerror(errno));
        if(attempt_count >= MAX_CONN_ATTEMPTS) {
            ERR_DIE("Maximum number of attempts exceeded\n");
        }
        attempt_count++;
        sleep(CONN_ATTEMPT_DELAY);
    }

    // Set socket to nonblocking
    SYSCALL(sock_flags, fcntl(sock_fd, F_GETFL), "getting sock_flags\n");
    SYSCALL(sock_flags, fcntl(sock_fd, F_SETFL, sock_flags|O_NONBLOCK),
        "setting socket flags\n");

    LOG_DEBUG("connected to server\n");
    
    pthread_exit(NULL);
}

int main(int argc, char const* argv[]) {
    int sock_fd, err = 0;
    char msgbuf[MSG_SIZE];
    pthread_t sig_tid;
    pthread_attr_t sig_attr;
    sigset_t sigset;

    // Initializing mutexes, conds and thread attributes
    if(pthread_attr_init(&sig_attr) < 0) ERR_DIE("Initializing thread attributes\n");

    // Create signal set
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGINT);
    // Spawn signal handler thread
    signal_worker_opt_t signal_worker_opt = {
        sigset,
    };
    if(pthread_create(&sig_tid, &sig_attr,
                      signal_worker, &signal_worker_opt) < 0 )
        ERR_DIE("Creating signal handler thread\n");
    if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) < 0)
        ERR_DIE("Masking signals in main thread\n");

    while(1) {
        sleep(3);    printf("hello world!\n");

    }
}
