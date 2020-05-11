#ifndef util_h_INCLUDED
#define util_h_INCLUDED

#include <time.h>
#include <errno.h>    
#include "logger.h"

// ========== Logging Utilities ==========

// Print to stderr
#define ERR(...) { LOG_CRITICAL(__VA_ARGS__); }

// Print an error and die
#define ERR_DIE(...) { ERR(__VA_ARGS__);\
    exit(EXIT_FAILURE); }

// ========== System call utilities  ==========

// Whether to log or not syscalls
#ifndef LOG_SYSCALL
#define LOGCALL(msg) ;
#else
#define LOGCALL(msg) LOG_DEBUG(msg);
#endif

// Run a syscall, store the result and die on fail
#define SYSCALL(result, call, msg) \
    LOGCALL(msg);\
    if((result = call) == -1) \
    { int e = errno; char errs[1024]; strerror_r(e, &errs[0], 1024);\
     ERR("%s: %s\n", msg, errs); exit(e); }

// ========== Synchronization macros that die on fail  ==========

#define MTX_LOCK_DIE(mtx) \
    { int err = 0; if((err = pthread_mutex_lock(mtx)) != 0) {\
        ERR("error locking resource: %s\n", strerror(err)); exit(err);\
    } LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_DIE(mtx) \
    { int err = 0; if((err = pthread_mutex_unlock(mtx)) != 0) {\
        ERR("error locking resource: %s\n", strerror(err)); exit(err);\
    } LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_DIE(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        ERR("error signaling cond: %s\n", strerror(err)); exit(err);\
    } LOG_NEVER("COND VAR %p signaled\n", (void*)mtx);}

#define COND_BROADCAST_DIE(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        ERR("error signaling cond: %s\n", strerror(err)); exit(err);\
    } LOG_NEVER("COND VAR %p broadcasted\n", (void*)mtx);}

#define COND_WAIT_DIE(event, mtx) \
    { int err = 0; if((err = pthread_cond_wait(event, mtx)) != 0) {\
        ERR("error waiting for cond: %s\n", strerror(err)); exit(err);\
    } LOG_NEVER("COND VAR %p waiting\n", (void*)mtx);}


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
        ERR("error locking resource: %s\n", strerror(err)); pthread_exit((void*)&err);\
    } LOG_NEVER("MUTEX %p locked\n", (void*)mtx);}

#define MTX_UNLOCK_EXT(mtx) \
    { int err = 0; if((err = pthread_mutex_unlock(mtx)) != 0) {\
        ERR("error locking resource: %s\n", strerror(err)); pthread_exit((void*)&err);\
    } LOG_NEVER("MUTEX %p unlocked\n", (void*)mtx);}

#define COND_SIGNAL_EXT(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
    ERR("error signaling cond: %s\n", strerror(err)); pthread_exit((void*)&err);}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}
    
#define COND_BROADCAST_EXT(ev) \
    { int err = 0; if((err = pthread_cond_signal(ev)) != 0) {\
        ERR("error signaling cond: %s\n", strerror(err)); pthread_exit((void*)&err);\
    } LOG_NEVER("COND VAR %p broadcasted\n", (void*)ev);}

#define COND_WAIT_EXT(ev, mtx) \
    { int err = 0; if((err = pthread_cond_wait(ev, mtx)) != 0) {\
        ERR("error waiting for cond: %s\n", strerror(err)); pthread_exit((void*)&err);}\
    LOG_NEVER("COND VAR %p signaled\n", (void*)ev);}

// ========== Miscellaneous functions ==========

// Good function to sleep for msec milliseconds and 
// Resume when interrupted. Found here 
// https://stackoverflow.com/q/1157209/7240056
int msleep(long msec); 

#endif // util_h_INCLUDED

