#ifndef util_h_INCLUDED
#define util_h_INCLUDED

#include "logger.h"

// Print to stderr
#define ERR(...) LOG_CRITICAL(__VA_ARGS__);

// Print an error and die
#define ERR_DIE(...) ERR(__VA_ARGS__);\
    exit(EXIT_FAILURE);

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


#endif // util_h_INCLUDED

