#include <stdlib.h> 
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <limits.h>

#include "globals.h"
#include "ini.h"
#include "cashcust.h"
#include "config.h"
#include "util.h"

// ========== Signal Handler ==========

// Contains options passed to the signal handler worker
typedef struct signal_worker_opt_s {
    // Signal set to be handled
    sigset_t sigset;
    // Array of client pids
    pid_t *client_pids;
    pthread_mutex_t* client_pids_mtx;
    int manager_pool_size;
} signal_worker_opt_t;

void* signal_worker (void* arg) {
    signal_worker_opt_t opt = *(signal_worker_opt_t*) arg;
    int err, signum;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0)
        ERR_DIE("Masking signals in connection thread");

    // Wait for the signals
    while(!should_quit) {
        SYSCALL_DIE(err, sigwait(&opt.sigset, &signum), "waiting for signals\n");
        // Forward SIGHUP (gentle quit) and SIGQUIT (brutal)
        // To connected clients
        LOG_DEBUG("Intercepted Signal %s\n", strsignal(signum));
        if (signum == SIGHUP || signum == SIGQUIT) {
            MTX_LOCK_DIE(opt.client_pids_mtx);
            for(int i = 0; i < opt.manager_pool_size; i++)
                if(opt.client_pids[i] > 0) {
                    LOG_DEBUG("Killing process %d with signal %s\n",
                        opt.client_pids[i], strsignal(signum));
                    kill(opt.client_pids[i], signum);
                }
            MTX_UNLOCK_DIE(opt.client_pids_mtx);
        } 
        should_quit = 1;
        goto signal_worker_exit;
    }

    signal_worker_exit:
        pthread_exit(NULL);
}


// Contains options passed to connection workers
typedef struct conn_opt_s {
    // File descriptor
    int fd;
    // Thread Number
    int id;
    // Array of client_pids 
    pid_t *client_pids;
    pthread_mutex_t* client_pids_mtx;
    // Signals to be ignored (already handled)
    sigset_t sigset;
    // Count of running processes
    int* running_count;
    pthread_mutex_t *count_mtx;
    pthread_cond_t *can_spawn_thread_event;
    // Other data
    int num_cashiers;
    long undercrowded_cash_treshold;
    long overcrowded_cash_treshold;
    int *running_arr;
} conn_opt_t;

void* conn_worker(void* arg) {
    conn_opt_t *opt = (conn_opt_t*) arg;
    char msgbuf[MSG_SIZE] = {0};
    ssize_t nread = 0, nwrote;
    int err = 0;
    printf("CASHIERS NUM = %d\n", opt->num_cashiers);
    long *queue_size_arr = calloc(opt->num_cashiers, sizeof(long));

    for(size_t i = 0; i < opt->num_cashiers; i++) {
        queue_size_arr[i] = -1;
    }
    queue_size_arr[0] = 0;
    if(pthread_sigmask(SIG_BLOCK, &opt->sigset, NULL) < 0)
        ERR_DIE("Masking signals in connection thread");

    
    MTX_LOCK_DIE(opt->count_mtx);
    *(opt->running_count) = *(opt->running_count) + 1;
    opt->running_arr[opt->id] = 1; 
    MTX_UNLOCK_DIE(opt->count_mtx);

    LOG_DEBUG("Worker %d socket connected\n", opt->id);
    while(!should_quit) {
    if ((nread = recvn(opt->fd, msgbuf, MSG_SIZE, 0) > 0)) {
        LOG_DEBUG("Worker %d fd %d received message: %s",
                    opt->id, opt->fd, msgbuf);
        
        if(strcmp(msgbuf, HELLO_BOSS) == 0) {
            LOG_DEBUG("Received connection request\n");
            // Read the supermarket process pid and store it in the array.
            // It is needed to forward signals.
            memset(msgbuf, 0, MSG_SIZE);
            if((nread = recvn(opt->fd, msgbuf, MSG_SIZE, 0)) > 0) {
                // Expect the process PID
                pid_t pid = (pid_t) strtol(msgbuf, NULL, 10);
                if (pid <= 0) {
                    LOG_DEBUG("Could not convert PID msg to int. Ignoring\n");
                    continue;
                }
                MTX_LOCK_EXT(opt->client_pids_mtx);
                if (opt->client_pids[opt->id] > 0) {
                    LOG_DEBUG("PID %d already connected to worker %d", 
                        opt->client_pids[opt->id], opt->id);
                    err = EALREADY;
                    goto conn_worker_exit;
                }
                // Push the process PID into clients table
                opt->client_pids[opt->id] = pid;
                memset(msgbuf, 0, MSG_SIZE);
                strncpy(msgbuf, MSG_CONN_ESTABLISHED, MSG_SIZE);
                LOG_DEBUG("Worker %d fd %d sending message: %s",
                          opt->id, opt->fd, msgbuf);
                if ((nwrote = sendn(opt->fd, msgbuf, MSG_SIZE, 0)) <= 0) {
                    ERR("Sending connection confirm\n");
                    goto conn_worker_exit;
                }
                memset(msgbuf, 0, MSG_SIZE);
                LOG_DEBUG("Worker %d successfully connected to process %d\n",
                    opt->id, opt->client_pids[opt->id]);
                MTX_UNLOCK_EXT(opt->client_pids_mtx);
            }
            
        // ========== Handle customer exit requests ==========
            
        } else if (strncmp(msgbuf, MSG_CUST_HEADER,
                           strlen(MSG_CUST_HEADER)) == 0) {
                               
        char *remaining = NULL;
        long cust_id = 0; 
        errno = 0;
        cust_id = strtol(&msgbuf[strlen(MSG_CUST_HEADER)],
               &remaining, 10); 
        if(cust_id < 0 || errno == ERANGE) {
            LOG_DEBUG("Received invalid customer ID: %ld\n", cust_id);
            memset(msgbuf, 0, MSG_SIZE);
            continue;
        } else if (msgbuf == remaining) {
            LOG_DEBUG("Malformed message: no cust ID\n");
            memset(msgbuf, 0, MSG_SIZE);
            continue;
        }
        // Should now do any additional checks


        // Send exit confirmation
        memset(msgbuf, 0, MSG_SIZE);
        snprintf(msgbuf, MSG_SIZE, "%s %ld %s\n",
                 MSG_CUST_HEADER, cust_id, MSG_GET_OUT);
        
        if((nwrote = sendn(opt->fd, msgbuf, MSG_SIZE, 0)) <= 0) {
            err = errno;
            free(msgbuf);
            ERR("Error sending message\n");
            goto conn_worker_exit;
        }
        
        // ========== Handle size polls ==========

        } else if (strncmp(msgbuf, MSG_QUEUE_SIZE,
                          strlen(MSG_QUEUE_SIZE)) == 0) {

            char *before_parsing = &msgbuf[strlen(MSG_QUEUE_SIZE)];
            char *after_parsing = NULL;
            int received = 0;
            long queue_size = 0;


            while(received < opt->num_cashiers) {
                // Read all the cashiers queue sizes sequentially
                // fail if there is not enough in the message 

                errno = 0;
                queue_size= strtol(before_parsing, &after_parsing, 10); 
                if(queue_size < -1 || errno == ERANGE) {
                    free(msgbuf);
                    ERR_SET_GOTO(conn_worker_exit, err,
                      "Received invalid queue_size %ld\n", queue_size);
                    
                } else if (before_parsing == after_parsing) {
                    should_quit = 1;
                    ERR_SET_GOTO(conn_worker_exit, err,
                      "Not enough cashiers in size poll\n");
                }

                queue_size_arr[received] = queue_size;
                before_parsing = after_parsing;
                received++;
            }

            int overcrowded_cashier = -1, first_closed = -1,
                least_crowded = -1, least_crowded_size = INT_MAX;
            int undercrowded_count = 0, open_cashiers = 1;

           
            for(int i = 0; i < opt->num_cashiers; i++) {
                if (queue_size_arr[i] == -1 && first_closed == -1) {
                    first_closed = i;
                }
                else {
                    if (queue_size_arr[i] >= 0
                        && (least_crowded == -1
                            || queue_size_arr[i] < least_crowded_size)) {
                        least_crowded = i;
                        least_crowded_size = queue_size_arr[i];
                    }
                    open_cashiers++;
                } 
                if(queue_size_arr[i] > 0 &&
                   queue_size_arr[i] >= opt->overcrowded_cash_treshold) {
                    overcrowded_cashier = i;
                } else if(queue_size_arr[i] >= 0 && 
                          queue_size_arr[i] <= 1) {
                    undercrowded_count++;
                }
            }

            printf("first_closed = %d, undercrowded_count = %d, tresh = %ld\n, open_cash %d, least_cr %d\n",
             first_closed, undercrowded_count, opt->undercrowded_cash_treshold, open_cashiers, least_crowded);
             if (undercrowded_count >=
                       opt->undercrowded_cash_treshold) {
                LOG_DEBUG("SHOULD CLOSEW!!!!!\n");
                if(open_cashiers > 1) {
                    if(least_crowded == -1) {
                        ERR("Internal logic error\n");
                        free(msgbuf);
                        goto conn_worker_exit;
                    }
                    memset(msgbuf, 0, MSG_SIZE);
                    snprintf(msgbuf, MSG_SIZE, "%s %d %s\n", 
                         MSG_CASH_HEADER, least_crowded, MSG_CLOSE_CASH);

                    LOG_DEBUG("Sending message: %s\n", msgbuf);

                    if((nwrote = sendn(opt->fd, msgbuf, MSG_SIZE, 0)) <= 0) {
                        err = errno;
                        free(msgbuf);
                        ERR("Error sending message\n");
                        goto conn_worker_exit;
                    }
                }
            } else if(first_closed != -1 && overcrowded_cashier >= 0) {
                memset(msgbuf, 0, MSG_SIZE);
                snprintf(msgbuf, MSG_SIZE, "%s %d %s\n", 
                         MSG_CASH_HEADER, first_closed, MSG_OPEN_CASH);

                LOG_DEBUG("Sending message: %s\n", msgbuf);
                if((nwrote = sendn(opt->fd, msgbuf, MSG_SIZE, 0)) <= 0) {
                    err = errno;
                    free(msgbuf);
                    ERR("Error sending message\n");
                    goto conn_worker_exit;
                }
            }
        } else {
        // ========== Other cases  ==========
            LOG_DEBUG("Unrecognised message\n");
        }
        // Reset buffer after reading
        memset(msgbuf, 0, MSG_SIZE);
    } else if (nread == -1) {
        err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            ERR("Worker %d. Error receiving data: %s\n", opt->id, strerror(err));
            goto conn_worker_exit;
        }
    } else if (nread == 0) {
        goto conn_worker_exit;
    }
    } // while 1
    
    
conn_worker_exit:
    MTX_LOCK_EXT(opt->count_mtx);
    *(opt->running_count) = *(opt->running_count) - 1;
    opt->running_arr[opt->id] = 0;
    COND_SIGNAL_EXT(opt->can_spawn_thread_event);
    MTX_UNLOCK_EXT(opt->count_mtx);
    // Negative pids are ignored when forwarding signals
    MTX_LOCK_EXT(opt->client_pids_mtx);
    opt->client_pids[opt->id] = -1;
    MTX_UNLOCK_EXT(opt->client_pids_mtx);
    close(opt->fd);
    free(queue_size_arr);
    pthread_exit(NULL);
}

int main(int argc, char *const argv[]) {
    // Current thread
    int c_thr = 0;
    int sock_fd, conn_fd, err = 0,
        manager_pool_size = 2;
    struct sockaddr_un addr;
    ini_t *config;
    pthread_t *conn_tid, sig_tid;
    pthread_attr_t *conn_attrs, sig_attr;
    pid_t *client_pids;
    sigset_t sigset;
    int *running_count = calloc(1, sizeof(int));
    *running_count = 0;
    pthread_mutex_t *client_pids_mtx = calloc(1, sizeof(pthread_mutex_t)),
                    *count_mtx = calloc(1, sizeof(pthread_mutex_t));
    pthread_cond_t *can_spawn_thread_event = calloc(1, sizeof(pthread_cond_t));
    char socket_path[UNIX_MAX_PATH] = {0}, 
         config_path[PATH_MAX] = {0};
    bool curr_accepted = false;

    int num_cashiers = DEFAULT_NUM_CASHIERS;
    long undercrowded_cash_treshold = DEFAULT_UNDERCROWDED_CASH_TRESHOLD;
    long overcrowded_cash_treshold = DEFAULT_OVERCROWDED_CASH_TRESHOLD;

    conn_opt_t *opt = NULL;

    // Tells which threads are running
    int *running_arr = NULL;
    
    // Create signal set
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) < 0)
        ERR_DIE("Masking signals in main thread\n");

    // ========== Read config file ==========
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


    // Set default strings
    strncpy(socket_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);

    if(access(config_path, F_OK) == -1) {
        err = errno;
        ERR_SET_GOTO(main_exit_1, err, "Could not open config file %s: %s",
                     config_path, strerror(err));
    }


    config = ini_load(config_path);
    ini_sget(config, NULL, "socket_path", "%d", &socket_path);
    if(strlen(socket_path) <= 0) {
        ERR("Invalid socket path\n");
        goto main_exit_1;
    }
    ini_sget(config, NULL, "num_cashiers", "%d", &num_cashiers);
    if(num_cashiers <= 0) {
        ERR("num_cashiers must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }

    ini_sget(config, NULL, "undercrowded_cash_treshold", "%ld",
             &undercrowded_cash_treshold);
    if(undercrowded_cash_treshold <= 0) {
        ERR("undercrowded_cash_treshold must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }
    ini_sget(config, NULL, "overcrowded_cash_treshold", "%ld",
             &overcrowded_cash_treshold);
    if(overcrowded_cash_treshold <= 0) {
        ERR("overcrowded_cash_treshold must be a positive integer\n");
        ini_free(config);
        goto main_exit_1;
    }

    ini_free(config);

    // ========== Data initialization ==========

    conn_tid = calloc(manager_pool_size, sizeof(pthread_t));
    conn_attrs = calloc(manager_pool_size, sizeof(pthread_attr_t));
    client_pids = calloc(manager_pool_size, sizeof(pid_t));
    opt = calloc(manager_pool_size, sizeof(conn_opt_t));
    running_arr = calloc(manager_pool_size, sizeof(int));

    for(int i = 0; i < manager_pool_size; i++) {
        conn_tid[i] = 0;
        client_pids[i] = 0;
        running_arr[i] = 0;
        if((err = pthread_attr_init(&conn_attrs[i])) != 0)
            ERR_DIE("Initializing thread attributes: %s\n", strerror(err));
        // if((err = pthread_attr_setdetachstate(&conn_attrs[i],
                                              // PTHREAD_CREATE_DETACHED)) != 0)
            // ERR_DIE("Initializing thread attributes: %s\n", strerror(err));
    }

    // Initializing mutexes, conds and thread attributes
    if(pthread_attr_init(&sig_attr) < 0)
        ERR_DIE("Initializing thread attributes\n");
    if(pthread_mutex_init(count_mtx, NULL) < 0)
        ERR_DIE("Allocating mutex\n");
    if(pthread_mutex_init(client_pids_mtx, NULL) < 0)
        ERR_DIE("Allocating mutex\n");
    if(pthread_cond_init(can_spawn_thread_event, NULL) < 0)
        ERR_DIE("Allocating cond var\n");

    // Spawnsignal handler thread
    signal_worker_opt_t signal_worker_opt = {
        sigset,
        client_pids,
        client_pids_mtx,
        manager_pool_size,
    };
    if(pthread_create(&sig_tid, &sig_attr, signal_worker, &signal_worker_opt) 
        < 0 ) ERR_DIE("Creating signal handler thread\n");

    // Init unix socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, UNIX_MAX_PATH);
    unlink(socket_path);
    // At first accept as non blocking then reset to blocking
    SYSCALL_SET_GOTO(sock_fd, socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0), 
                     "Creating socket\n", err, main_exit_2);
    SYSCALL_SET_GOTO(err, bind(sock_fd, (struct sockaddr*) &addr, 
                     sizeof(addr)), "Binding socket\n", err, main_exit_2);
    SYSCALL_SET_GOTO(err, listen(sock_fd, manager_pool_size),
                     "Listening on socket\n", err, main_exit_2);

    while (!should_quit) {
        MTX_LOCK_DIE(count_mtx);
        while(*running_count >= manager_pool_size) {
            LOG_DEBUG("CONNECTION POOL FULL! WAITING!\n");
            COND_WAIT_DIE(can_spawn_thread_event, count_mtx);
        }
        // (*running_count) = *running_count + 1;
        // running_arr[c_thr] = 1;
        MTX_UNLOCK_DIE(count_mtx);
        curr_accepted = false;
        LOG_DEBUG("Waiting for connection...\n");
        while(!should_quit && !curr_accepted) {
           conn_fd = accept(sock_fd, NULL, 0);
           if (conn_fd == -1) {
               err = errno;
               if (err != EAGAIN && err != EWOULDBLOCK) {
                   ERR("Accepting connection\n");
                   goto main_exit_2;
               }
           } else {
               curr_accepted = true;
           }
        }
        if(should_quit) goto main_exit_3;

        opt[c_thr].fd = conn_fd;
        opt[c_thr].id = c_thr;
        opt[c_thr].client_pids = client_pids;
        opt[c_thr].client_pids_mtx = client_pids_mtx;
        opt[c_thr].sigset = sigset;
        opt[c_thr].running_count = running_count;
        opt[c_thr].count_mtx = count_mtx;
        opt[c_thr].can_spawn_thread_event = can_spawn_thread_event;
        opt[c_thr].num_cashiers = num_cashiers;
        opt[c_thr].undercrowded_cash_treshold = undercrowded_cash_treshold;
        opt[c_thr].overcrowded_cash_treshold = overcrowded_cash_treshold;
        opt[c_thr].running_arr = running_arr;
        err = pthread_create(&conn_tid[c_thr], &conn_attrs[c_thr],
                             conn_worker, opt);
        if(err != 0) ERR_DIE("Spawning thread: %s", strerror(err));
        c_thr = (c_thr + 1) % manager_pool_size;
    }

main_exit_3:
    pthread_join(sig_tid, NULL);
main_exit_2:
    should_quit = 1;
    for(int i = 0; i < manager_pool_size; i++) {
        LOG_DEBUG("Thread %d is running? %d\n", i, running_arr[i]);
        if(running_arr[i] == 1) {
            LOG_DEBUG("Joining connection worker %d\n", i);
            pthread_join(conn_tid[i], NULL);
            pthread_attr_destroy(&conn_attrs[i]);
        }
    }
    free(opt);
    free(conn_attrs);
    free(can_spawn_thread_event);
    free(conn_tid);
    free(running_arr);
    free(client_pids);
main_exit_1:
    pthread_attr_destroy(&sig_attr);
    free(running_count);
    free(count_mtx);
    free(client_pids_mtx);
    unlink(socket_path);
    return 0;
}
