#include <stdlib.h> 
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "globals.h"
#include "config.h"
#include "util.h"
#include "conc_lqueue.h"
#include "cashcust.h"


// ========== Signal Handler Worker ==========

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
        MTX_LOCK_DIE(&flags_mtx);
        if (signum == SIGHUP) should_close = 1;
        else should_quit = 1;
        MTX_UNLOCK_DIE(&flags_mtx);
        pthread_exit(NULL);
   }
   exit(EXIT_FAILURE);
}

// ========== Outbound Message Worker ==========

// Contains options passed to the messaging thread
typedef struct msg_worker_opt_s {
    // Signals to be blocked
    sigset_t sigset;
    // Connection socket file descriptor
    int sock_fd;
    // message queue
    conc_lqueue_t *msgqueue; 
} msg_worker_opt_t;


void* outmsg_worker(void* arg) {
    msg_worker_opt_t opt = *(msg_worker_opt_t *)arg;
    char *msgbuf = malloc(MSG_SIZE);
    ssize_t sent;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0) {
        ERR("Masking signals in connection thread");
        goto outmsg_worker_exit;
    }

    memset(msgbuf, 0, MSG_SIZE);
    strncpy(msgbuf, HELLO_BOSS, MSG_SIZE);
    SYSCALL(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE), "sending header\n");
    memset(msgbuf, 0, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "%d\n", getpid());
    
    SYSCALL(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE), "sending pid\n");
    free(msgbuf);

    // Pop messages from queue and send them
    while(conc_lqueue_dequeue(opt.msgqueue, (void *) &msgbuf) == 0) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, outmsg_worker_exit);

        LOG_DEBUG("Sending message: %s\n", msgbuf);
        SYSCALL(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE),
            "sending message\n");
        free(msgbuf);
    }

outmsg_worker_exit:
    pthread_exit(NULL);
}

// ========== Inbound Message Worker ==========

void* inmsg_worker(void* arg) {
    msg_worker_opt_t opt = *(msg_worker_opt_t *)arg;
    char statbuf[MSG_SIZE];
    char *msgbuf = NULL;
    ssize_t received;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0) {
        ERR("Masking signals in connection thread");
        goto inmsg_worker_exit;
    }

    while(1) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, inmsg_worker_exit);

        received = readn(opt.sock_fd, statbuf, MSG_SIZE);
        if (received == 0) {
            LOG_DEBUG("Detected closed socket\n");
            goto inmsg_worker_exit;
        } else if (received < 0) {
            LOG_CRITICAL("Error while reading socket: %s\n", strerror(errno));
            goto inmsg_worker_exit;
        }

        if(conc_lqueue_closed(opt.msgqueue)) {
            LOG_DEBUG("Detected closed queue\n");
            goto inmsg_worker_exit;  
        } 
        LOG_DEBUG("Received message: %s\n", statbuf);
        msgbuf = malloc(MSG_SIZE);
        memset(msgbuf, 0, MSG_SIZE);
        strncpy(msgbuf, statbuf, MSG_SIZE);
        if(conc_lqueue_enqueue(opt.msgqueue, (void*) msgbuf) != 0) {
            LOG_DEBUG("Error while enqueueing message\n");
            goto inmsg_worker_exit;  
        }
    }

inmsg_worker_exit:
    pthread_exit(NULL);
}


// ========== Main Thread ==========

int main(int argc, char const* argv[]) {
    int sock_fd, sock_flags = 0, conn_attempt_count = 0, err = 0,
        customer_count = 0;
    pthread_t sig_tid, outmsg_tid, inmsg_tid;
    pthread_attr_t sig_attr, outmsg_attr, inmsg_attr;
    sigset_t sigset;
    char *msgbuf;
    conc_lqueue_t *outmsgqueue = NULL, *inmsgqueue = NULL;
    struct sockaddr_un addr;

    pthread_t *customer_tid_arr = NULL;
    pthread_attr_t *customer_attr_arr = NULL;
    customer_opt_t *customer_opt_arr = NULL;
    bool *customer_terminated_arr = NULL;

    pthread_t *cashier_tid_arr = NULL;
    pthread_attr_t *cashier_attr_arr = NULL;
    // Array of flags to tell which threads are joinable
    cashier_opt_t *cashier_opt_arr = NULL;

    pthread_mutex_t customer_count_mtx;
    // ========== Parse configuration file ==========
    // TODO parse configuration file

    // ========== Data initialization ==========

    if(pthread_attr_init(&sig_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");
    if(pthread_attr_setdetachstate(&sig_attr, PTHREAD_CREATE_DETACHED) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");
    if(pthread_attr_init(&outmsg_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");
    if(pthread_attr_init(&inmsg_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Masking signals in main thread\n");

    outmsgqueue = conc_lqueue_init();
    inmsgqueue = conc_lqueue_init();

   // ========== Spawn signal handler thread  ==========
    
    signal_worker_opt_t signal_worker_opt = {
        sigset,
    };
    if(pthread_create(&sig_tid, &sig_attr,
                      signal_worker, &signal_worker_opt) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Creating signal handler thread\n");

    // ========== Connect to server process  ==========
    
    // Prepare socket addr
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);
        SYSCALL(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0), "creating socket\n");
    while(connect(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, main_exit_1);
        LOG_CRITICAL("Error connecting to server: %s\n", strerror(errno));
        if(conn_attempt_count >= MAX_CONN_ATTEMPTS) {
            ERR_SET_GOTO(main_exit_1, err, "Max number of attempts exceeded\n");
        }
        conn_attempt_count++;
        msleep(CONN_ATTEMPT_DELAY);
    }
    // Set socket to nonblocking
    SYSCALL(sock_flags, fcntl(sock_fd, F_GETFL), "getting sock_flags\n");
    SYSCALL(sock_flags, fcntl(sock_fd, F_SETFL, sock_flags|O_NONBLOCK),
        "setting socket flags\n");
    LOG_DEBUG("connected to server\n");
    
    // ========== Initialize message handler threads and queue ==========

    msg_worker_opt_t outmsg_opt = {
        sigset,
        sock_fd,
        outmsgqueue,
    };
    if(pthread_create(&outmsg_tid, &outmsg_attr,
                      outmsg_worker, &outmsg_opt) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Creating outbound msg worker\n");
    
    msg_worker_opt_t inmsg_opt = {
        sigset,
        sock_fd,
        inmsgqueue,
    };
    if(pthread_create(&inmsg_tid, &inmsg_attr, inmsg_worker, &inmsg_opt) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Creating inbound msg worker\n");

    // ========== Creating cashiers. Only 1 is open at startup  ==========

    if (NUM_CASHIERS <= 1) 
        ERR_SET_GOTO(main_exit_2, err, "NUM_CASHIERS must be >= 0\n");
    cashier_tid_arr = malloc(sizeof(pthread_t) * CUST_CAP);
    cashier_attr_arr = malloc(sizeof(pthread_attr_t) * CUST_CAP);
    cashier_opt_arr = malloc(sizeof(cashier_opt_t) * CUST_CAP);
    for(int i = 0; i < NUM_CASHIERS; i++) {
        pthread_attr_init(&cashier_attr_arr[i]);
        cashier_init(&cashier_opt_arr[i], i, outmsgqueue);
        if (i == 0) *(cashier_opt_arr[i].state) = OPEN;
        if(pthread_create(&cashier_tid_arr[i], &cashier_attr_arr[i], 
                          cashier_worker, &cashier_opt_arr[i]) < 0)
            ERR_SET_GOTO(main_exit_2, err, "Creating cashier worker\n");
    }


    // ========== Creating first customers ==========
    if (CUST_CAP <= 1)
        ERR_SET_GOTO(main_exit_2, err, "CUST_CAP must be >= 0\n");

    if((err = pthread_mutex_init(&customer_count_mtx, NULL)) != 0)
        ERR_SET_GOTO(main_exit_2, err, "Allocating Mutex %s", strerror(err));

    customer_tid_arr = malloc(sizeof(pthread_t) * CUST_CAP);
    customer_attr_arr = malloc(sizeof(pthread_attr_t) * CUST_CAP);
    customer_opt_arr = malloc(sizeof(customer_opt_t) * CUST_CAP);
    customer_terminated_arr = malloc(CUST_CAP);

    for(int i = 0; i < CUST_CAP; i++) {
        pthread_attr_init(&customer_attr_arr[i]);
        customer_terminated_arr[i] = false;
        customer_init(&customer_opt_arr[i], i, &customer_count,
                      &customer_count_mtx, cashier_opt_arr, &customer_terminated_arr[i]);
        
        if(pthread_create(&customer_tid_arr[i], &customer_attr_arr[i], 
                          customer_worker, &customer_opt_arr[i]) < 0)
            ERR_SET_GOTO(main_exit_2, err, "Creating customer worker\n");
        MTX_LOCK_DIE(&customer_count_mtx);
        customer_count++;
        MTX_UNLOCK_DIE(&customer_count_mtx);
    }

    // ========== Main loop ==========

    while(1) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, main_exit_3);
        // Check if the number of customers has got below C - E
        MTX_LOCK_DIE(&customer_count_mtx);
        if((CUST_CAP - customer_count) > CUST_BATCH && !should_close) {
            LOG_DEBUG("Letting more customers in\n");
            for(int i = 0; i < CUST_CAP; i++) {
            if(customer_terminated_arr[i]) {
                customer_terminated_arr[i] = false;
                pthread_join(customer_tid_arr[i], NULL);
                customer_destroy(&customer_opt_arr[i]);
                pthread_attr_destroy(&customer_attr_arr[i]);
                pthread_attr_init(&customer_attr_arr[i]);
                customer_init(&customer_opt_arr[i], i, &customer_count,
                              &customer_count_mtx, cashier_opt_arr,
                              &customer_terminated_arr[i]);
                if(pthread_create(&customer_tid_arr[i],
                                  &customer_attr_arr[i], 
                                  customer_worker,
                                  &customer_opt_arr[i]) < 0)
                ERR_SET_GOTO(main_exit_2, err,
                                   "Creating customer worker\n");
                customer_count++;
            }
            }
        }
        MTX_UNLOCK_DIE(&customer_count_mtx);
        msleep(MAIN_POLL_TIME);
    }

    // ========== Cleanup  ==========
    main_exit_3: 
        LOG_DEBUG("Joining customer threads\n");
        for(int i = 0; i < CUST_CAP; i++) {
            LOG_DEBUG("Joining customer thread %d\n", i);
            COND_SIGNAL_DIE(customer_opt_arr[i].state_change_event);
            pthread_join(customer_tid_arr[i], NULL);
            customer_destroy(&customer_opt_arr[i]);
            pthread_attr_destroy(&customer_attr_arr[i]);
        }
        LOG_DEBUG("Joining cashier threads\n");
        for(int i = 0; i < NUM_CASHIERS; i++) {
            LOG_DEBUG("Joining cashier thread %d\n", i);
            pthread_join(cashier_tid_arr[i], NULL);
            cashier_destroy(&cashier_opt_arr[i]);
            pthread_attr_destroy(&cashier_attr_arr[i]);
        }
        free(customer_terminated_arr);
        free(customer_tid_arr);
        free(customer_attr_arr);
        free(customer_opt_arr);
        free(cashier_tid_arr);
        free(cashier_attr_arr);
        free(cashier_opt_arr);
    main_exit_2:
        LOG_DEBUG("Closing message queue\n");
        conc_lqueue_close(outmsgqueue);
        conc_lqueue_close(inmsgqueue);
        LOG_DEBUG("Joining out message handler worker\n");
        pthread_join(outmsg_tid, NULL);
        LOG_DEBUG("Joining in message handler worker\n");
        pthread_join(inmsg_tid, NULL);
        // LOG_DEBUG("Closing file descriptors\n");
        // close(sock_fd);
    main_exit_1:
        LOG_DEBUG("Final cleanups... \n");
        conc_lqueue_destroy(outmsgqueue);
        conc_lqueue_destroy(inmsgqueue);
        exit(err);
}
