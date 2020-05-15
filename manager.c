#include <stdlib.h> 
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <limits.h>

#include "ini.h"
#include "config.h"
#include "util.h"

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
    while(1) {
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
            exit(0);
        } else exit(0);
    }
    // Should never get here
    exit(EXIT_FAILURE);
}


// Contains options passed to connection workers
typedef struct conn_opt_s {
    // File descriptor
    int fd;
    // Thread Number
    int id;
    // Return status
    int *status;
    // Array of client_pids 
    pid_t *client_pids;
    pthread_mutex_t* client_pids_mtx;
    // Signals to be ignored (already handled)
    sigset_t sigset;
    // Count of running processes
    int* running_count;
    pthread_mutex_t *count_mtx;
    pthread_cond_t *can_spawn_thread_event;
} conn_opt_t;

void* conn_worker(void* arg) {
    conn_opt_t opt = *(conn_opt_t*) arg;
    char msgbuf[MSG_SIZE] = {0};
    ssize_t nread, nwrote;
    int err = 0;

    if(pthread_sigmask(SIG_BLOCK, &opt.sigset, NULL) < 0)
        ERR_DIE("Masking signals in connection thread");

    LOG_DEBUG("Worker %d socket connected\n", opt.id);
    while((nread = readn(opt.fd, msgbuf, MSG_SIZE) > 0)) {
        LOG_DEBUG("Worker %d fd %d received message: %s",
                    opt.id, opt.fd, msgbuf);
        if(strcmp(msgbuf, HELLO_BOSS) == 0) {
            LOG_DEBUG("Received connection request\n");
            // Read the supermarket process pid and store it in the array.
            // It is needed to forward signals.
            memset(msgbuf, 0, MSG_SIZE);
            if((nread = readn(opt.fd, msgbuf, MSG_SIZE)) > 0) {
                // Expect the process PID
                pid_t pid = (pid_t) strtol(msgbuf, NULL, 10);
                if (pid <= 0) {
                    LOG_DEBUG("Could not convert PID msg to int. Ignoring\n");
                    continue;
                }
                MTX_LOCK_EXT(opt.client_pids_mtx);
                if (opt.client_pids[opt.id] != 0) {
                    LOG_DEBUG("PID %d already connected to worker %d", 
                        opt.client_pids[opt.id], opt.id);
                    err = EALREADY;
                    goto conn_worker_exit;
                }
                // Push the process PID into clients table
                opt.client_pids[opt.id] = pid;
                strncpy(msgbuf, MSG_CONN_ESTABLISHED, MSG_SIZE);
                LOG_DEBUG("Worker %d fd %d sending message: %s",
                          opt.id, opt.fd, msgbuf);
                if ((nwrote = writen(opt.fd, msgbuf, MSG_SIZE)) <= 0) {
                    ERR("Sending connection confirm\n");
                    goto conn_worker_exit;
                }
                memset(msgbuf, 0, MSG_SIZE);
                LOG_DEBUG("Worker %d successfully connected to process %d\n", 
                    opt.id, opt.client_pids[opt.id]);
                MTX_UNLOCK_EXT(opt.client_pids_mtx);
            }
        } else {
            LOG_DEBUG("Unrecognised message\n");
        }
        // Reset buffer after reading
        memset(msgbuf, 0, MSG_SIZE);
    }
    if (nread == -1) {
        err = errno;
        ERR("Worker %d. Error receiving data: %s\n", opt.id, strerror(err));
        goto conn_worker_exit;
    }

    
conn_worker_exit:
    MTX_LOCK_EXT(opt.count_mtx);
    *(opt.running_count) = *(opt.running_count) - 1;
    COND_SIGNAL_EXT(opt.can_spawn_thread_event);
    MTX_UNLOCK_EXT(opt.count_mtx);
    // Negative pids are ignored when forwarding signals
    MTX_LOCK_EXT(opt.client_pids_mtx);
    opt.client_pids[opt.id] = -1;
    MTX_UNLOCK_EXT(opt.client_pids_mtx);
    *(opt.status) = err;
    pthread_exit(NULL);
}

int main(int argc, char const* argv[]) {
    // Current thread
    int c_thr = 0;
    int sock_fd, conn_fd, err = 0,
        manager_pool_size = 2;
    struct sockaddr_un addr;
    ini_t *config;
    pthread_t *conn_tid, sig_tid;
    pthread_attr_t *conn_attrs, sig_attr;
    int *statuses;
    pid_t *client_pids;
    pthread_mutex_t client_pids_mtx;
    sigset_t sigset;
    int running_count = 0;
    pthread_mutex_t count_mtx;
    pthread_cond_t can_spawn_thread_event;
    char socket_path[UNIX_MAX_PATH] = {0}, 
         config_path[PATH_MAX] = {0};

    // ========== Read config file ==========
    // TODO get config file from arg
    
    // Set default strings
    strncpy(socket_path, DEFAULT_SOCK_PATH, UNIX_MAX_PATH);

    strncpy(config_path, DEFAULT_CONFIG_PATH, PATH_MAX);
    if(access(config_path, F_OK) == -1) {
        err = errno;
        ERR_SET_GOTO(main_exit_1, err, "Could not open config file %s: %s",
                     config_path, strerror(err));
    }

    config = ini_load(config_path);
    ini_sget(config, NULL, "manager_pool_size", "%d", &manager_pool_size);
    if(manager_pool_size <= 0) {
        ERR("manager_pool_size must be a positive integer\n");
        goto main_exit_1;
    }
    ini_sget(config, NULL, "socket_path", "%d", &socket_path);
    if(strlen(socket_path) <= 0) {
        ERR("Invalid socket path\n");
        goto main_exit_1;
    }

    ini_free(config);

    // ========== Data initialization ==========

    conn_tid = malloc(manager_pool_size * sizeof(pthread_t));
    conn_attrs = malloc(manager_pool_size * sizeof(pthread_attr_t));
    statuses = malloc(manager_pool_size * sizeof(int));
    client_pids = malloc(manager_pool_size * sizeof(pid_t));

    for(int i = 0; i < manager_pool_size; i++) {
        conn_tid[i] = 0;
        statuses[i] = 0;
        client_pids[i] = 0;
        if((err = pthread_attr_init(&conn_attrs[i])) != 0)
            ERR_DIE("Initializing thread attributes: %s\n", strerror(err));
        if((err = pthread_attr_setdetachstate(&conn_attrs[i],
                                              PTHREAD_CREATE_DETACHED)) != 0)
            ERR_DIE("Initializing thread attributes: %s\n", strerror(err));

    }

    // Initializing mutexes, conds and thread attributes
    if(pthread_attr_init(&sig_attr) < 0)
        ERR_DIE("Initializing thread attributes\n");
    if(pthread_mutex_init(&count_mtx, NULL) < 0)
        ERR_DIE("Allocating mutex\n");
    if(pthread_mutex_init(&client_pids_mtx, NULL) < 0)
        ERR_DIE("Allocating mutex\n");
    if(pthread_cond_init(&can_spawn_thread_event, NULL) < 0)
        ERR_DIE("Allocating cond var\n");

    // Create signal set
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &sigset, NULL) < 0)
        ERR_DIE("Masking signals in main thread\n");
    // Spawnsignal handler thread
    signal_worker_opt_t signal_worker_opt = {
        sigset,
        client_pids,
        &client_pids_mtx,
        manager_pool_size,
    };
    if(pthread_create(&sig_tid, &sig_attr, signal_worker, &signal_worker_opt) 
        < 0 ) ERR_DIE("Creating signal handler thread\n");

    // Init unix socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, UNIX_MAX_PATH);
    unlink(socket_path);
    SYSCALL_SET_GOTO(sock_fd, socket(AF_UNIX, SOCK_STREAM, 0), 
                     "Creating socket\n", err, main_exit_1);
    SYSCALL_SET_GOTO(err, bind(sock_fd, (struct sockaddr*) &addr, 
                     sizeof(addr)), "Binding socket\n", err, main_exit_1);
    SYSCALL_SET_GOTO(err, listen(sock_fd, manager_pool_size),
                     "Listening on socket\n", err, main_exit_1);

    while (1) {
        MTX_LOCK_DIE(&count_mtx);
        while(running_count >= manager_pool_size) {
            LOG_DEBUG("CONNECTION POOL FULL! WAITING!\n");
            COND_WAIT_DIE(&can_spawn_thread_event, &count_mtx);
        }
        running_count++;
        MTX_UNLOCK_DIE(&count_mtx);
        LOG_DEBUG("Waiting for connection...\n");
        SYSCALL_SET_GOTO(conn_fd, accept(sock_fd, NULL, 0),
                        "Accepting connection\n", err, main_exit_1);
        conn_opt_t opt = { 
            conn_fd,
            c_thr,
            &statuses[c_thr],
            client_pids,
            &client_pids_mtx,
            sigset,
            &running_count,
            &count_mtx,
            &can_spawn_thread_event
        };
        err = pthread_create(&conn_tid[c_thr], &conn_attrs[c_thr],
                             conn_worker, &opt);
        if(err != 0) ERR_DIE("Spawning thread: %s", strerror(err));
        c_thr = (c_thr + 1) % manager_pool_size;
    }

main_exit_1:
    unlink(socket_path);
    return 0;
}
