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
#include <limits.h>

#include "ini.h"
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
        SYSCALL_DIE(err, sigwait(&opt.sigset, &signum),
                    "waiting for signals\n");
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
    int err;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0) {
        ERR("Masking signals in connection thread");
        goto outmsg_worker_exit;
    }

    memset(msgbuf, 0, MSG_SIZE);
    strncpy(msgbuf, HELLO_BOSS, MSG_SIZE);
    SYSCALL_SET_GOTO(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE),
                     "sending header\n", err, outmsg_worker_exit);
    memset(msgbuf, 0, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "%d\n", getpid());
    
    SYSCALL_SET_GOTO(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE),
                     "sending pid\n", err, outmsg_worker_exit);
    free(msgbuf);

    // Pop messages from queue and send them
    while(conc_lqueue_dequeue(opt.msgqueue, (void *) &msgbuf) == 0) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, outmsg_worker_exit);

        LOG_DEBUG("Sending message: %s\n", msgbuf);
        SYSCALL_SET_GOTO(sent, writen(opt.sock_fd, msgbuf, MSG_SIZE),
            "sending message\n", err, outmsg_worker_exit);
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
    char *msgbuf, config_path[PATH_MAX] = {0};
    conc_lqueue_t *outmsgqueue = NULL, *inmsgqueue = NULL;
    struct sockaddr_un addr;
    ini_t *config;
    
    pthread_t *customer_tid_arr = NULL;
    pthread_attr_t *customer_attr_arr = NULL;
    customer_opt_t *customer_opt_arr = NULL;
    bool *customer_terminated_arr = NULL;
    pthread_t *cashier_tid_arr = NULL;
    pthread_attr_t *cashier_attr_arr = NULL;
    // Array of flags to tell which threads are joinable
    cashier_opt_t *cashier_opt_arr = NULL;
    pthread_mutex_t customer_count_mtx;

    // Values read from config file with defaults
    int max_conn_attempts = DEFAULT_MAX_CONN_ATTEMPTS;
    int product_cap = DEFAULT_PRODUCT_CAP;
    long conn_attempt_delay = DEFAULT_CONN_ATTEMPT_DELAY;
    long cashier_poll_time = DEFAULT_CASHIER_POLL_TIME;
    long time_per_prod = DEFAULT_TIME_PER_PROD;
    long max_shopping_time = DEFAULT_MAX_SHOPPING_TIME;
    long supermarket_poll_time = DEFAULT_SUPERMARKET_POLL_TIME;
    size_t num_cashiers = DEFAULT_NUM_CASHIERS;
    size_t cust_cap = DEFAULT_CUST_CAP;
    size_t cust_batch = DEFAULT_CUST_BATCH;


    // ========== Parse configuration file ==========
    // TODO get config file from arg

    strncpy(config_path, DEFAULT_CONFIG_PATH, PATH_MAX);
    if(access(config_path, F_OK) == -1) {
        err = errno;
        ERR_SET_GOTO(main_exit_1, err, "Could not open config file %s: %s",
                     config_path, strerror(err));
    }

    config = ini_load(config_path);
    ini_sget(config, NULL, "max_conn_attempts", "%d", &max_conn_attempts); 
    if(max_conn_attempts <= 0) {
        ERR("max_conn_attempts must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "conn_attempt_delay", "%ld", &conn_attempt_delay);
    if(conn_attempt_delay <= 0) {
        ERR("conn_attempt_delay must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "num_cashiers", "%zu", &num_cashiers); 
    if(num_cashiers <= 0) {
        ERR("num_cashiers must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "cust_cap", "%zu", &cust_cap); 
    if(cust_cap <= 0) {
        ERR("cust_cap must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "cashier_poll_time", "%ld", &cashier_poll_time);
    if(cashier_poll_time <= 0) {
        ERR("cashier_poll_time must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "time_per_prod", "%ld", &time_per_prod);
    if(time_per_prod <= 0) {
        ERR("time_per_prod must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "max_shopping_time", "%ld", &max_shopping_time);
    if(max_shopping_time <= 0) {
        ERR("max_shopping_time must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "product_cap", "%ld", &product_cap);
    if(product_cap <= 0) {
        ERR("product_cap must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "supermarket_poll_time", "%ld",
             &supermarket_poll_time);
    if(supermarket_poll_time <= 0) {
        ERR("supermarket_poll_time must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }

    ini_free(config);

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
    // Ignore SIGPIPE in case the server socket closes
    // earlier.
    sigaddset(&sigset, SIGPIPE);
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
    SYSCALL_SET_GOTO(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0),
                     "creating socket\n", err, main_exit_1);
    while(connect(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        CHECK_FLAG_GOTO(should_quit, &flags_mtx, main_exit_1);
        LOG_CRITICAL("Error connecting to server: %s\n", strerror(errno));
        if(conn_attempt_count >= max_conn_attempts) {
            ERR_SET_GOTO(main_exit_1, err, "Max number of attempts exceeded\n");
        }
        conn_attempt_count++;
        msleep(conn_attempt_delay);
    }
    // Set socket to nonblocking
    SYSCALL_SET_GOTO(sock_flags, fcntl(sock_fd, F_GETFL),
                     "getting sock_flags\n", err, main_exit_1);
    SYSCALL_SET_GOTO(sock_flags, fcntl(sock_fd, F_SETFL, sock_flags|O_NONBLOCK),
                     "setting socket flags\n", err, main_exit_1);
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

    cashier_tid_arr = malloc(sizeof(pthread_t) * cust_cap);
    cashier_attr_arr = malloc(sizeof(pthread_attr_t) * cust_cap);
    cashier_opt_arr = malloc(sizeof(cashier_opt_t) * cust_cap);
    for(size_t i = 0; i < num_cashiers; i++) {
        pthread_attr_init(&cashier_attr_arr[i]);
        cashier_init(&cashier_opt_arr[i], i, outmsgqueue,
                     cashier_poll_time, time_per_prod);
        if (i == 0) *(cashier_opt_arr[i].state) = OPEN;
        if(pthread_create(&cashier_tid_arr[i], &cashier_attr_arr[i], 
                          cashier_worker, &cashier_opt_arr[i]) < 0)
            ERR_SET_GOTO(main_exit_2, err, "Creating cashier worker\n");
    }


    // ========== Creating first customers ==========
    if((err = pthread_mutex_init(&customer_count_mtx, NULL)) != 0)
        ERR_SET_GOTO(main_exit_2, err, "Allocating Mutex %s", strerror(err));

    customer_tid_arr = malloc(sizeof(pthread_t) * cust_cap);
    customer_attr_arr = malloc(sizeof(pthread_attr_t) * cust_cap);
    customer_opt_arr = malloc(sizeof(customer_opt_t) * cust_cap);
    customer_terminated_arr = malloc(cust_cap);

    for(size_t i = 0; i < cust_cap; i++) {
        pthread_attr_init(&customer_attr_arr[i]);
        customer_terminated_arr[i] = false;
        customer_init(&customer_opt_arr[i], i, &customer_count,
                      &customer_count_mtx, cashier_opt_arr,
                      &customer_terminated_arr[i],
                      max_shopping_time, product_cap, 
                      num_cashiers);
        
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
        MTX_LOCK_DIE(&flags_mtx);
        if (should_close) {
           if(customer_count == 0) {
                MTX_UNLOCK_DIE(&flags_mtx);
                MTX_UNLOCK_DIE(&customer_count_mtx);
                goto main_exit_3;
           }
        }
        else if((cust_cap - customer_count) > cust_batch) {
            LOG_DEBUG("Letting more customers in\n");
            for(int i = 0; i < cust_cap; i++) {
            if(customer_terminated_arr[i]) {
                customer_terminated_arr[i] = false;
                pthread_join(customer_tid_arr[i], NULL);
                customer_destroy(&customer_opt_arr[i]);
                pthread_attr_destroy(&customer_attr_arr[i]);
                pthread_attr_init(&customer_attr_arr[i]);
                customer_init(&customer_opt_arr[i], i, &customer_count,
                              &customer_count_mtx, cashier_opt_arr,
                              &customer_terminated_arr[i],
                              max_shopping_time, product_cap,
                              num_cashiers);
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
        MTX_UNLOCK_DIE(&flags_mtx);
        MTX_UNLOCK_DIE(&customer_count_mtx);
        msleep(supermarket_poll_time);
    }

    // ========== Cleanup  ==========
    main_exit_3: 
        LOG_DEBUG("Joining customer threads\n");
        for(size_t i = 0; i < cust_cap; i++) {
            LOG_DEBUG("Joining customer thread %zu\n", i);
            MTX_LOCK_DIE(customer_opt_arr[i].state_mtx);
            COND_SIGNAL_DIE(customer_opt_arr[i].state_change_event);
            MTX_UNLOCK_DIE(customer_opt_arr[i].state_mtx);
            pthread_join(customer_tid_arr[i], NULL);
            customer_destroy(&customer_opt_arr[i]);
            pthread_attr_destroy(&customer_attr_arr[i]);
        }
        LOG_DEBUG("Joining cashier threads\n");
        for(int i = 0; i < num_cashiers; i++) {
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
    main_exit_1:
        LOG_DEBUG("Final cleanups... \n");
        conc_lqueue_destroy(outmsgqueue);
        conc_lqueue_destroy(inmsgqueue);
        exit(err);
}
