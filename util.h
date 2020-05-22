#ifndef util_h_INCLUDED
#define util_h_INCLUDED

#include <stdlib.h>
#include "logger.h"

// ========== Miscellaneous Macros  ==========

// Print to stderr
#define ERR(...) { LOG_CRITICAL(__VA_ARGS__); }

// Print an error and die
#define ERR_DIE(...) { ERR(__VA_ARGS__);\
    exit(EXIT_FAILURE); }

// Print an error, set var and goto a label
#define ERR_SET_GOTO(lab, var, ...) {\
    ERR(__VA_ARGS__); var = EXIT_FAILURE; goto lab;}

// Use POSIX random because of a better distribution
// than rand.
#define RAND_RANGE(seed, low, up) \
    ((rand_r(seed) % (up - low + 1)) + low)

// ========== System call utilities  ==========

// Whether to log or not syscalls
#ifndef LOG_SYSCALL
#define LOGCALL(msg) ;
#else
#define LOGCALL(msg) LOG_DEBUG(msg);
#endif

// Run a syscall, store the result and die on fail
#define SYSCALL_DIE(result, call, msg) \
    LOGCALL(msg);\
    if((result = call) == -1) \
    { int e = errno; char errs[1024]; strerror_r(e, &errs[0], 1024);\
     ERR("%s: %s\n", msg, errs); exit(e); }

// Run a syscall, store the result set a value and goto on fail
#define SYSCALL_SET_GOTO(result, call, msg, set, lab) \
    LOGCALL(msg);\
    if((result = call) == -1) \
    { set = errno; char errs[1024]; strerror_r(set, &errs[0], 1024);\
     ERR("%s: %s\n", msg, errs); goto lab; }

// ========== Synchronization macros that die on fail  ==========

#define MTX_LOCK_DIE(mtx) \
    { int err = 0; if((err = pthread_mutex_lock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); exit(err);\
    } LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_DIE(mtx) \
    { int err = 0; if((err = pthread_mutex_unlock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); exit(err);\
    } LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_DIE(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error signaling cond: %s\n", errs); exit(err);\
    } LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}

#define COND_BROADCAST_DIE(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error signaling cond: %s\n", errs); exit(err);\
    } LOG_NEVER("COND VAR %p broadcasted\n", (void*)ev);}

#define COND_WAIT_DIE(event, mtx) \
    { int err = 0; if((err = pthread_cond_wait(event, mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error waiting for cond: %s\n", errs); exit(err);\
    } LOG_NEVER("COND VAR %p waiting\n", (void*)event);}


// ========== Synchronization macros that return instead of dying  ==========

#define MTX_LOCK_RET(mtx) \
    { int err = 0; \
    if((err = pthread_mutex_lock(mtx)) != 0) {\
    LOG_CRITICAL("error locking mutex %p\n", (void*) mtx); return err;}\
    LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_RET(mtx) \
    { int err = 0; \
    if((err = pthread_mutex_unlock(mtx)) != 0) {\
    LOG_CRITICAL("error unlocking mutex %p\n", (void*) mtx); return err;}\
    LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_RET(ev) \
    { int err = 0; \
    if((err = pthread_cond_signal(ev)) != 0) {\
    LOG_CRITICAL("error signaling condition %p\n", (void*) ev); return err;}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}

#define COND_BROADCAST_RET(ev) \
    { int err = 0; \
    if((err = pthread_cond_broadcast(ev)) != 0) {\
    LOG_CRITICAL("error broadcasting condition %p\n", (void*) ev); return err;}\
    LOG_NEVER("COND VAR %p broadcasted\n", (void*)ev);}

#define COND_WAIT_RET(ev, m) \
    { int err = 0; \
    if((err = pthread_cond_wait(ev, m)) != 0) {\
    LOG_CRITICAL("error waiting condition %p\n", (void*) ev); return err;}\
    LOG_NEVER("COND VAR %p waiting\n", (void*)ev);}


        
// ========== Synchronization macros that exit thread on fail  ==========

#define MTX_LOCK_EXT(mtx) \
    { int err = 0; if((err = pthread_mutex_lock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); pthread_exit((void*)&err);\
    } LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_EXT(mtx) \
    { int err = 0; if((err = pthread_mutex_unlock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); pthread_exit((void*)&err);\
    } LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_EXT(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
    ERR("error signaling cond: %s\n", errs); pthread_exit((void*)&err);}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}
    
#define COND_BROADCAST_EXT(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error signaling cond: %s\n", errs); pthread_exit((void*)&err);\
    } LOG_NEVER("COND VAR %p broadcasted\n", (void*)ev);}

#define COND_WAIT_EXT(ev, mtx) \
    { int err = 0; if((err = pthread_cond_wait(ev, mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error waiting for cond: %s\n", errs); pthread_exit((void*)&err);}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}


// ========== Synchronization macros that goto a label on fail ==========

#define MTX_LOCK_GOTO(mtx, lab) \
    { int err = 0; if((err = pthread_mutex_lock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); goto lab;\
    } LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_GOTO(mtx, lab) \
    { int err = 0; if((err = pthread_mutex_unlock(mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error locking resource: %s\n", errs); goto lab;\
    } LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_GOTO(ev, lab) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
    ERR("error signaling cond: %s\n", errs); goto lab;}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}
    
#define COND_BROADCAST_GOTO(ev, lab) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error signaling cond: %s\n", errs); goto lab;\
    } LOG_NEVER("COND VAR %p broadcasted\n", (void*)ev);}

#define COND_WAIT_GOTO(ev, mtx, lab) \
    { int err = 0; if((err = pthread_cond_wait(ev, mtx)) != 0) {\
        char errs[1024] = {0}; strerror_r(err, errs, 1024);\
        ERR("error waiting for cond: %s\n", errs); goto lab;}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}


// ========== Miscellaneous Functions ==========

// From https://stackoverflow.com/q/1157209/7240056
// Sleep for msec milliseconds and resume if interrupted
// by a syscall
int msleep(long msec);

// Wrappers to read and write to avoid "short" operations
// From "Advanced Programming In the UNIX Environment" 

ssize_t  /* Read "n" bytes from a descriptor */
readn(int fd, void *ptr, size_t n);

ssize_t  /* Write "n" bytes to a descriptor */
writen(int fd, void *ptr, size_t n);

ssize_t  /* Receive "n" bytes from a socket */
recvn(int fd, void *ptr, size_t n, int flags);

ssize_t  /* Write "n" bytes to a socket*/
sendn(int fd, void *ptr, size_t n, int flags);

#endif // util_h_INCLUDED
