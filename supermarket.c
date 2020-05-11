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

static volatile char should_quit = 0;


typedef struct signal_worker_opt_s {
    // Signal set to be handled
    sigset_t sigset;
} signal_worker_opt_t;

// This thread waits for SIGHUP to perform a gentle exit
void* signal_worker(void* arg) {
    signal_worker_opt_t opt = *(signal_worker_opt_t *) arg;
    int err, signum;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0)
        ERR_DIE("Masking signals in connection thread");

    while(1) {
        SYSCALL(err, sigwait(&opt.sigset, &signum), "waiting for signals\n");
        LOG_DEBUG("Intercepted Signal %s\n", strsignal(signum));
        // TODO if (signum == SIGHUP) signal other threads to quit gently;
        // else if (signum == SIGQUIT) exit brutally (?)
        // exit(0);
        should_quit = 1;
        pthread_exit(NULL);
   }
   exit(EXIT_FAILURE);
}

// Contains options passed to the messaging thread
typedef struct msg_worker_opt_s {
    // Signals to be blocked
    sigset_t sigset;
    conc_lqueue_t *msgqueue; 
} msg_worker_opt_t;


void* msg_worker(void* arg) {
    msg_worker_opt_t opt = *(msg_worker_opt_t *)arg;
    int sock_fd, attempt_count = 0, sock_flags = 0, queue_stat = 0;
    char *msgbuf = malloc(MSG_SIZE);
    ssize_t sent;
    struct sockaddr_un addr;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0)
        ERR_DIE("Masking signals in connection thread");

    // Prepare socket addr
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);
    
    SYSCALL(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0), "creating socket\n");
    while(connect(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        if(should_quit) { goto msg_worker_exit; };
        LOG_CRITICAL("Error connecting to server: %s\n", strerror(errno));
        if(attempt_count >= MAX_CONN_ATTEMPTS) {
            ERR_DIE("Maximum number of attempts exceeded\n");
        }
        attempt_count++;
        msleep(CONN_ATTEMPT_DELAY);
    }

    // Set socket to nonblocking
    SYSCALL(sock_flags, fcntl(sock_fd, F_GETFL), "getting sock_flags\n");
    SYSCALL(sock_flags, fcntl(sock_fd, F_SETFL, sock_flags|O_NONBLOCK),
        "setting socket flags\n");

    LOG_DEBUG("connected to server\n");

    memset(msgbuf, 0, MSG_SIZE);
    strncpy(msgbuf, HELLO_BOSS, MSG_SIZE);
    SYSCALL(sent, send(sock_fd, msgbuf, MSG_SIZE, 0), "sending header\n");
    memset(msgbuf, 0, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "%d\n", getpid());
    
    SYSCALL(sent, send(sock_fd, msgbuf, MSG_SIZE, 0), "sending pid\n");
    free(msgbuf);

    // Pop messages from queue and send them
    while(conc_lqueue_dequeue(opt.msgqueue, (void *) &msgbuf) == 0) {
        if(should_quit) { goto msg_worker_exit; };
        if(conc_lqueue_closed(opt.msgqueue)) { goto msg_worker_exit; };
        LOG_DEBUG("Sending message: %s\n", msgbuf);
        SYSCALL(sent, send(sock_fd, msgbuf, MSG_SIZE, 0), "sending message\n");
        free(msgbuf);
    }

msg_worker_exit:
    close(sock_fd);
    pthread_exit(NULL);
}

int main(int argc, char const* argv[]) {
    pthread_t sig_tid, msg_tid, err = 0;
    pthread_attr_t sig_attr, msg_attr;
    sigset_t sigset;
    char *msgbuf;
    conc_lqueue_t *msgqueue;

    // Initializing mutexes, conds and thread attributes
    if(pthread_attr_init(&sig_attr) < 0)
        ERR_DIE("Initializing thread attributes\n");
    if(pthread_attr_init(&msg_attr) < 0)
        ERR_DIE("Initializing thread attributes\n");

    // Create signal set
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) < 0)
        ERR_DIE("Masking signals in main thread\n");

    // Spawn signal handler thread
    signal_worker_opt_t signal_worker_opt = {
        sigset,
    };
    if(pthread_create(&sig_tid, &sig_attr, signal_worker, &signal_worker_opt)
       < 0 ) ERR_DIE("Creating signal handler thread\n");

    // Initialize message handler thread and message queue
    msgqueue = conc_lqueue_init();
    msg_worker_opt_t msg_worker_opt = {
        sigset,
        msgqueue,
    };
    if(pthread_create(&msg_tid, &msg_attr, msg_worker, &msg_worker_opt)
       < 0 ) ERR_DIE("Creating signal handler thread\n");

    while(1) {
        if(should_quit) {
            LOG_NOTICE("Exiting gracefully \n");
            LOG_DEBUG("Closing message queue\n");
            conc_lqueue_close(msgqueue);
            LOG_DEBUG("Joining signal handler worker\n");
            pthread_join(sig_tid, NULL);
            LOG_DEBUG("Joining message handler worker\n");
            pthread_join(msg_tid, NULL);
            LOG_DEBUG("Final cleanups... \n");
            conc_lqueue_destroy(msgqueue);
            exit(0);
        }
        msgbuf = malloc(MSG_SIZE);
        memset(msgbuf, 0, MSG_SIZE);
        snprintf(msgbuf, MSG_SIZE, "hello world %d!\n", rand());
        conc_lqueue_enqueue(msgqueue, msgbuf);
        msleep(3000);
    }
}
