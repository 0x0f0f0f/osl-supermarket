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


// ========== Signal Handler ==========

static void signal_handler(int sig) {
    if (sig == SIGHUP) should_close = 1;
    else should_quit = 1;
}

// ========== Outbound Message Worker ==========

// Contains options passed to the messaging thread
typedef struct msg_worker_opt_s {
    // Connection socket file descriptor
    int sock_fd;
    // message queue
    conc_lqueue_t *msgqueue;
    int cust_cap;
    customer_opt_t *customer_opt_arr;
    int num_cashiers;
    pthread_mutex_t *cashier_mtx_arr;
    bool *cashier_isopen_arr;
    cashier_opt_t *cashier_opt_arr;
    pthread_t *cashier_tid_arr;
    long time_per_prod;
    pthread_attr_t *cashier_attr_arr;
} msg_worker_opt_t;


void* outmsg_worker(void* arg) {
    msg_worker_opt_t opt = *(msg_worker_opt_t *)arg;
    char *msgbuf = NULL;
    ssize_t sent;
    int err;

    while(1) {
        if (should_quit) goto outmsg_worker_exit;
        // Pop messages from queue and send them
        if (conc_lqueue_closed(opt.msgqueue)) {
            LOG_DEBUG("Detected closed queue\n");
            goto outmsg_worker_exit;  
        }

        if(conc_lqueue_dequeue_nonblock(opt.msgqueue, (void *) &msgbuf) == 0) {
            LOG_DEBUG("Sending message: %s", msgbuf);
            if (sendn(opt.sock_fd, msgbuf, MSG_SIZE, 0) == -1) {
                err = errno;
                free(msgbuf);
                ERR("Error sending message: %s", strerror(err));
                goto outmsg_worker_exit;
            }
            free(msgbuf);
        }

        // TODO use proper passive waiting
        msleep(2);
    }

outmsg_worker_exit:
    pthread_exit(NULL);
}

// ========== Inbound Message Worker ==========

void* inmsg_worker(void* arg) {
    msg_worker_opt_t opt = *(msg_worker_opt_t *)arg;
    char statbuf[MSG_SIZE];
    int err;
    char *msgbuf = NULL;
    ssize_t received;

    while(1) {
        if (should_quit) goto inmsg_worker_exit;

        received = recvn(opt.sock_fd, statbuf, MSG_SIZE, 0);
        if (received == 0) {
            LOG_DEBUG("Detected closed socket\n");
            goto inmsg_worker_exit;
        } else if (received < 0) {
            err = errno;
            if (err != EINTR)
            LOG_CRITICAL("Error while reading socket: %s\n", strerror(err));
            goto inmsg_worker_exit;
        }

        if(conc_lqueue_closed(opt.msgqueue)) {
            LOG_DEBUG("Detected closed queue\n");
            goto inmsg_worker_exit;  
        } 
        LOG_DEBUG("Received message: %s", statbuf);
        msgbuf = calloc(1, MSG_SIZE);
        strncpy(msgbuf, statbuf, MSG_SIZE);


// ========== Customer Exit Confirmation  ==========
        // NOTE: customers can be kicked out by the manager

        if(strncmp(msgbuf, MSG_CUST_HEADER,
                   strlen(MSG_CUST_HEADER)) == 0) {
            char *remaining = NULL;
            long cust_id = 0; 
            errno = 0;
            cust_id = strtol(&msgbuf[strlen(MSG_CUST_HEADER)],
                   &remaining, 10); 
            if(cust_id < 0 || cust_id >= opt.cust_cap) {
                LOG_DEBUG("Received invalid customer ID: %ld\n", cust_id);
                memset(msgbuf, 0, MSG_SIZE);
                continue;
            } else if (msgbuf == remaining) {
                LOG_DEBUG("Malformed message: no cust ID\n");
                memset(msgbuf, 0, MSG_SIZE);
                continue;
            }
            // Remove spaces
            while(*remaining == ' ') remaining++;

            if(strncmp(remaining, MSG_GET_OUT,
                       strlen(MSG_GET_OUT)) == 0) {
                MTX_LOCK_DIE(opt.customer_opt_arr[cust_id].state_mtx);
                *(opt.customer_opt_arr[cust_id].state) = CAN_EXIT;
                COND_SIGNAL_DIE(opt.customer_opt_arr[cust_id]
                                .state_change_event);
                MTX_UNLOCK_DIE(opt.customer_opt_arr[cust_id].state_mtx);
            }

        } else if (strncmp(msgbuf, MSG_CASH_HEADER,
                           strlen(MSG_CASH_HEADER)) == 0) {

// ========== Cashier Opening/Closing ==========

            LOG_DEBUG("Received a cash operation\n");
                              
            char *remaining = NULL;
            long cash_id = 0; 
            errno = 0;
            cash_id = strtol(&msgbuf[strlen(MSG_CASH_HEADER)],
                   &remaining, 10); 
            if(cash_id < 0 || cash_id >= opt.num_cashiers) {
                LOG_DEBUG("Received invalid cash ID: %ld\n", cash_id);
                memset(msgbuf, 0, MSG_SIZE);
                continue;
            } else if (msgbuf == remaining) {
                LOG_DEBUG("Malformed message: no cash ID\n");
                memset(msgbuf, 0, MSG_SIZE);
                continue;
            }
            // Remove spaces
            while(*remaining == ' ') remaining++;
            if(strncmp(remaining, MSG_OPEN_CASH,
                       strlen(MSG_OPEN_CASH)) == 0) {

// ========== Cashier Opening  ==========
                 
                MTX_LOCK_DIE(&opt.cashier_mtx_arr[cash_id]);
                if(opt.cashier_isopen_arr[cash_id]) {
                    ERR("Cashier %ld already open\n",
                              cash_id);
                    MTX_UNLOCK_DIE(&opt.cashier_mtx_arr[cash_id]);
                    continue;
                }
                opt.cashier_isopen_arr[cash_id] = true;
                MTX_UNLOCK_DIE(&opt.cashier_mtx_arr[cash_id]);

                // Spawn first cashier thread
                LOG_DEBUG("Opening cashier %ld\n", cash_id);

                cashier_init(&opt.cashier_opt_arr[cash_id], cash_id,
                             &opt.cashier_isopen_arr[cash_id],
                             &opt.cashier_mtx_arr[cash_id],
                             opt.time_per_prod);

                if(pthread_create(&opt.cashier_tid_arr[cash_id],
                                  &opt.cashier_attr_arr[cash_id], 
                                  cashier_worker,
                                  &opt.cashier_opt_arr[cash_id]) < 0)
                ERR_SET_GOTO(inmsg_worker_exit, err,
                             "Creating cashier worker\n");


            } else if(strncmp(remaining, MSG_CLOSE_CASH,
                       strlen(MSG_CLOSE_CASH)) == 0) {

// ========== Cashier Closing ==========
                
                MTX_LOCK_DIE(&opt.cashier_mtx_arr[cash_id]);
                if(opt.cashier_isopen_arr[cash_id] == false) {
                    ERR("Cashier already closed %ld\n",
                              cash_id);
                    MTX_UNLOCK_DIE(&opt.cashier_mtx_arr[cash_id]);
                    continue;
                }
                opt.cashier_isopen_arr[cash_id] = false;
                MTX_UNLOCK_DIE(&opt.cashier_mtx_arr[cash_id]);

                LOG_DEBUG("Closing cashier %ld\n", cash_id);

                // Reschedule customers
                customer_opt_t *curr_cust = NULL;
                while((err = conc_lqueue_dequeue_nonblock(
                        opt.cashier_opt_arr[cash_id].custqueue,
                        (void*) &curr_cust)) == 0) {
                    customer_reschedule(curr_cust);
                } 
                if (err != ELQUEUEEMPTY) {
                    ERR("Rescheduling customers\n"); 
                    free(msgbuf);
                    goto inmsg_worker_exit;
                }


                if(pthread_join(opt.cashier_tid_arr[cash_id], NULL) < 0) {
                   ERR("Joining cashier thread %ld\n", cash_id);   
                   free(msgbuf);
                   goto inmsg_worker_exit;
                }


                MTX_LOCK_DIE(&opt.cashier_mtx_arr[cash_id]);
                opt.cashier_isopen_arr[cash_id] = false;
                MTX_UNLOCK_DIE(&opt.cashier_mtx_arr[cash_id]);

                cashier_destroy(&opt.cashier_opt_arr[cash_id]);

            } else {
                LOG_DEBUG("Unrecognized message\n");
            }
        } else {
            LOG_DEBUG("Unrecognized message\n");
        }
        free(msgbuf);

        // if(conc_lqueue_enqueue(opt.msgqueue, (void*) msgbuf) != 0) {
        //     LOG_DEBUG("Error while enqueueing message\n");
        //     free(msgbuf);
        //     goto inmsg_worker_exit;  
        // }

    }


inmsg_worker_exit:
    pthread_exit(NULL);
}


// ========== Main Thread ==========

int main(int argc, char* const argv[]) {
    int sock_fd, conn_attempt_count = 0, err = 0,
        customer_count = 0;
    char *msgbuf, 
         statbuf[MSG_SIZE] = {0},
         config_path[PATH_MAX] = {0};
    conc_lqueue_t *outmsgqueue = NULL, *inmsgqueue = NULL;
    struct sockaddr_un addr;
    struct sigaction act;

    ini_t *config;
    size_t sent, received;

    pthread_t inmsg_tid, outmsg_tid;
    pthread_attr_t inmsg_attr, outmsg_attr;

    pthread_t *customer_tid_arr = NULL;
    pthread_attr_t *customer_attr_arr = NULL;
    customer_opt_t *customer_opt_arr = NULL;
    // Array of flags to tell which threads are joinable
    bool *customer_terminated_arr = NULL;

    pthread_t *cashier_tid_arr = NULL;
    pthread_attr_t *cashier_attr_arr = NULL;
    pthread_mutex_t *cashier_mtx_arr = NULL;
    cashier_opt_t *cashier_opt_arr = NULL;
    bool *cashier_isopen_arr = NULL;
    pthread_mutex_t customer_count_mtx;

    pthread_t cashier_poller_tid;
    pthread_attr_t cashier_poller_attr;
    cashier_poll_opt_t *cashier_poller_opt;
    

    // Values read from config file with defaults
    int max_conn_attempts = DEFAULT_MAX_CONN_ATTEMPTS;
    int product_cap = DEFAULT_PRODUCT_CAP;
    long conn_attempt_delay = DEFAULT_CONN_ATTEMPT_DELAY;
    long cashier_poll_time = DEFAULT_CASHIER_POLL_TIME;
    long time_per_prod = DEFAULT_TIME_PER_PROD;
    long max_shopping_time = DEFAULT_MAX_SHOPPING_TIME;
    long supermarket_poll_time = DEFAULT_SUPERMARKET_POLL_TIME;
    int num_cashiers = DEFAULT_NUM_CASHIERS;
    size_t cust_cap = DEFAULT_CUST_CAP;
    size_t cust_batch = DEFAULT_CUST_BATCH;

// ========== Data initialization ==========
    if(pthread_attr_init(&outmsg_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");
    if(pthread_attr_init(&inmsg_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");
    if(pthread_attr_init(&cashier_poller_attr) < 0)
        ERR_SET_GOTO(main_exit_1, err, "Initializing thread attributes\n");

    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler;
   
    outmsgqueue = conc_lqueue_init();
    inmsgqueue = conc_lqueue_init();

    SYSCALL_SET_GOTO(err, sigaction(SIGINT, &act, NULL),
                    "reg signal\n", err, main_exit_1);

    SYSCALL_SET_GOTO(err, sigaction(SIGQUIT, &act, NULL),
                    "reg signal\n", err, main_exit_1);

    SYSCALL_SET_GOTO(err, sigaction(SIGHUP, &act, NULL),
                    "reg signal\n", err, main_exit_1);
    
    SYSCALL_SET_GOTO(err, sigaction(SIGPIPE, &act, NULL),
                    "reg signal\n", err, main_exit_1);

// ========== Parse configuration file ==========
    int c;
    strncpy(config_path, DEFAULT_CONFIG_PATH, PATH_MAX);
    
    while((c = getopt(argc, argv, "c:")) != -1) {
        switch(c) {
            case 'c':
                strncpy(config_path, optarg, PATH_MAX);
            break;
            case '?':
                ERR("Unrecognized option: '-%c'\n", optopt);
                goto main_exit_1;
            break;
        } 
    }

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
    ini_sget(config, NULL, "num_cashiers", "%d", &num_cashiers); 
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
    ini_sget(config, NULL, "product_cap", "%d", &product_cap);
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

// ========== Connect to server process  ==========
    
    // Prepare socket addr
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);
    SYSCALL_SET_GOTO(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0),
                     "creating socket\n", err, main_exit_1);
    while(connect(sock_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        if (should_quit) goto main_exit_1;
        LOG_CRITICAL("Error connecting to server: %s\n", strerror(errno));
        if(conn_attempt_count >= max_conn_attempts) {
            ERR_SET_GOTO(main_exit_1, err, "Max number of attempts exceeded\n");
        }
        conn_attempt_count++;
        msleep(conn_attempt_delay);
    }

    // Set socket to nonblocking
    // SYSCALL_SET_GOTO(sock_flags, fcntl(sock_fd, F_GETFL),
    //                  "getting sock_flags\n", err, main_exit_1);
    // SYSCALL_SET_GOTO(sock_flags, fcntl(sock_fd, F_SETFL, sock_flags|O_NONBLOCK),
    //                  "setting socket flags\n", err, main_exit_1);

    LOG_NOTICE("Waiting for server connection\n");

// ========== Initial connection exchange  ==========

    msgbuf = calloc(1, MSG_SIZE);
    memset(msgbuf, 0, MSG_SIZE);
    strncpy(msgbuf, HELLO_BOSS, MSG_SIZE);
    SYSCALL_SET_GOTO(sent, sendn(sock_fd, msgbuf, MSG_SIZE, 0),
                     "sending header\n", err, main_exit_1);
    memset(msgbuf, 0, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "%d\n", getpid());
    
    SYSCALL_SET_GOTO(sent, sendn(sock_fd, msgbuf, MSG_SIZE, 0),
                     "sending pid\n", err, main_exit_1);

    SYSCALL_SET_GOTO(received, recvn(sock_fd, statbuf, MSG_SIZE, 0), 
                     "getting conn confirm\n", err, main_exit_1);

    if (strncmp(statbuf, MSG_CONN_ESTABLISHED, MSG_SIZE) != 0) {
        ERR("Could not connect");
        goto main_exit_1;
    }

    memset(statbuf, 0, MSG_SIZE);
    free(msgbuf);
    LOG_NOTICE("Connection Established\n");


// ========== Creating cashiers. Only 1 is open at startup  ==========

    cashier_tid_arr = calloc(num_cashiers, sizeof(pthread_t));
    cashier_attr_arr = calloc(num_cashiers, sizeof(pthread_attr_t));
    cashier_opt_arr = calloc(num_cashiers, sizeof(cashier_opt_t));
    cashier_mtx_arr = calloc(num_cashiers, sizeof(pthread_mutex_t));
    cashier_isopen_arr = calloc(num_cashiers, sizeof(bool));

    for(size_t i = 0; i < num_cashiers; i++) {
        pthread_attr_init(&cashier_attr_arr[i]);
        if((err = pthread_mutex_init(&cashier_mtx_arr[i], NULL)) != 0)
            ERR_SET_GOTO(main_exit_2, err, "Allocating Attr %s",
            strerror(err));
        if((err = pthread_attr_init(&cashier_attr_arr[i])) != 0)
            ERR_SET_GOTO(main_exit_2, err, "Allocating Mutex %s",
            strerror(err));

        cashier_isopen_arr[i] = false;
    }
    

    // Spawn first cashier thread
    cashier_isopen_arr[0] = true;
    cashier_init(&cashier_opt_arr[0], 0,
                 &cashier_isopen_arr[0],
                 &cashier_mtx_arr[0],
                 time_per_prod);
    if(pthread_create(&cashier_tid_arr[0], &cashier_attr_arr[0], 
                      cashier_worker, &cashier_opt_arr[0]) < 0)
        ERR_SET_GOTO(main_exit_2, err, "Creating cashier worker\n");
// ========== Creating first customers ==========

    if((err = pthread_mutex_init(&customer_count_mtx, NULL)) != 0)
        ERR_SET_GOTO(main_exit_2, err, "Allocating Mutex %s", strerror(err));

    customer_tid_arr = calloc(cust_cap, sizeof(pthread_t));
    customer_attr_arr = calloc(cust_cap, sizeof(pthread_attr_t));
    customer_opt_arr = calloc(cust_cap, sizeof(customer_opt_t));
    customer_terminated_arr = calloc(cust_cap, sizeof(bool));

    for(size_t i = 0; i < cust_cap; i++) {
        pthread_attr_init(&customer_attr_arr[i]);
        customer_terminated_arr[i] = false;
        customer_init(&customer_opt_arr[i], i,
                      &customer_count,
                      &customer_count_mtx,
                      cashier_opt_arr,
                      cashier_isopen_arr,
                      cashier_mtx_arr,
                      &customer_terminated_arr[i],
                      max_shopping_time,
                      product_cap, 
                      num_cashiers,
                      outmsgqueue);
        
        if(pthread_create(&customer_tid_arr[i], &customer_attr_arr[i], 
                          customer_worker, &customer_opt_arr[i]) < 0)
            ERR_SET_GOTO(main_exit_2, err, "Creating customer worker\n");
        MTX_LOCK_DIE(&customer_count_mtx);
        customer_count++;
        MTX_UNLOCK_DIE(&customer_count_mtx);
    }

// ========== Creating message handler threads ==========

    msg_worker_opt_t outmsg_opt = {
        sock_fd,
        outmsgqueue
    };
    msg_worker_opt_t inmsg_opt = {
        sock_fd,
        inmsgqueue,
        cust_cap,
        customer_opt_arr,
        num_cashiers,
        cashier_mtx_arr,
        cashier_isopen_arr,
        cashier_opt_arr,
        cashier_tid_arr,
        time_per_prod,
        cashier_attr_arr
    };

    if(pthread_create(&outmsg_tid, &outmsg_attr,
                      outmsg_worker, (void*) &outmsg_opt) < 0) {
        ERR_SET_GOTO(main_exit_2, err, "Creating msg worker\n");
    }
    if(pthread_create(&inmsg_tid, &inmsg_attr,
                      inmsg_worker, (void*) &inmsg_opt) < 0) {
        ERR_SET_GOTO(main_exit_2, err, "Creating msg worker\n");
    }

// ========== Creating additional threads ==========

    // Spawn cashier poll thread
    cashier_poller_opt = calloc(1, sizeof(cashier_poll_opt_t));
    cashier_poller_opt->cashier_arr = cashier_opt_arr;
    cashier_poller_opt->cashier_arr_size = num_cashiers;
    cashier_poller_opt->cashier_poll_time = cashier_poll_time;
    cashier_poller_opt->cashier_mtx_arr = cashier_mtx_arr;
    cashier_poller_opt->cashier_isopen_arr = cashier_isopen_arr;
    cashier_poller_opt->outmsgqueue = outmsgqueue;

    if(pthread_create(&cashier_poller_tid, &cashier_poller_attr,

                      cashier_poll_worker, cashier_poller_opt) < 0)
        ERR_SET_GOTO(main_exit_2, err, "Creating cashier poll worker\n");
                      

    //Spawn periodic re-enqueuer thread
    pthread_t customer_renqueue_worker_tid;
    pthread_attr_t *customer_renqueue_attr = calloc(1, sizeof(pthread_attr_t));
    customer_renqueue_worker_t *customer_renqueue_worker_opt 
        = calloc(1, sizeof(customer_renqueue_worker_t));
    customer_renqueue_worker_opt->cashier_arr = cashier_opt_arr;
    customer_renqueue_worker_opt->cashier_arr_size = num_cashiers;
    customer_renqueue_worker_opt->cashier_isopen_arr = cashier_isopen_arr;
    customer_renqueue_worker_opt->cashier_mtx_arr = cashier_mtx_arr;


    if(pthread_create(&customer_renqueue_worker_tid, customer_renqueue_attr,
                      customer_renqueue_worker, customer_renqueue_worker_opt) < 0)
        ERR_SET_GOTO(main_exit_2, err, "Creating customer renqueue worker");
        


// ========== Main loop ==========

    while(!should_quit) {

        // Check if the number of customers has got below C - E
        MTX_LOCK_DIE(&customer_count_mtx);
        // LOG_DEBUG("Customer count = %d\n", customer_count);

        // If the process received SIGHUP, wait until there are
        // No customers left then exit gently
        if (should_close) {
           if(customer_count == 0) {
                should_quit = 1;
                MTX_UNLOCK_DIE(&customer_count_mtx);
                goto main_exit_3;
           }
        }
        // Otherwise let cust_batch customers in
        else if((cust_cap - customer_count) > cust_batch) {
            LOG_DEBUG("Letting more customers in\n");
            for(int i = 0; i < cust_cap; i++) {
            if(customer_terminated_arr[i]) {
                customer_terminated_arr[i] = false;
                pthread_join(customer_tid_arr[i], NULL);
                customer_destroy(&customer_opt_arr[i]);
                pthread_attr_destroy(&customer_attr_arr[i]);
                pthread_attr_init(&customer_attr_arr[i]);
                customer_init(&customer_opt_arr[i], i, 
                              &customer_count,
                              &customer_count_mtx,
                              cashier_opt_arr,
                              cashier_isopen_arr,
                              cashier_mtx_arr,
                              &customer_terminated_arr[i],
                              max_shopping_time,
                              product_cap,
                              num_cashiers,
                              outmsgqueue);
                if(pthread_create(&customer_tid_arr[i],
                                  &customer_attr_arr[i], 
                                  customer_worker,
                                  &customer_opt_arr[i]) < 0)
                ERR_SET_GOTO(main_exit_3, err,
                                   "Creating customer worker\n");
                customer_count++;
            }
            }
        }
        MTX_UNLOCK_DIE(&customer_count_mtx);

// ========== Handle inbound messages ==========
        
        // if ((err = conc_lqueue_dequeue_nonblock(inmsgqueue,
        //                                         (void*) &msgbuf)) == 0)
            if(should_quit) goto main_exit_3;
            

        msleep(supermarket_poll_time);
    }

conc_lqueue_abort_all_operations = 1;
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
            if(cashier_isopen_arr[i]) {
                pthread_join(cashier_tid_arr[i], NULL);
                cashier_destroy(&cashier_opt_arr[i]);
                pthread_attr_destroy(&cashier_attr_arr[i]);
            }
        }
        pthread_attr_destroy(customer_renqueue_attr);
        free(customer_renqueue_attr);
        pthread_join(cashier_poller_tid, NULL);
        free(customer_terminated_arr);
        free(customer_tid_arr);
        free(customer_attr_arr);
        free(customer_opt_arr);
        free(cashier_tid_arr);
        free(cashier_attr_arr);
        free(cashier_mtx_arr);
        free(cashier_opt_arr);
        free(cashier_isopen_arr);
        free(customer_renqueue_worker_opt);
        free(cashier_poller_opt);
        pthread_join(inmsg_tid, NULL);
        pthread_join(outmsg_tid, NULL);
        pthread_join(customer_renqueue_worker_tid, NULL);
    main_exit_2:
        LOG_DEBUG("Closing message queue\n");
        conc_lqueue_close(outmsgqueue);
        conc_lqueue_close(inmsgqueue);
        conc_lqueue_destroy(outmsgqueue);
        conc_lqueue_destroy(inmsgqueue);
    main_exit_1:
        LOG_DEBUG("Final cleanups... \n");
        exit(err);
}
