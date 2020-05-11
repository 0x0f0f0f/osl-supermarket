#ifndef cashier_h_INCLUDED
#define cashier_h_INCLUDED

#include <unistd.h>

#include "conc_lqueue.h"

// ========== Data Types ==========

// Data type for cashier thread.
typedef struct cashier_worker_opt_s {
    int id;
    // 20-80ms
    useconds_t start_time;
    useconds_t time_per_product;
    // Concurrent customer queue
    conc_lqueue_t custqueue;
    // Outbound message queue
    conc_lqueue_t outmsgqueue;
    // Inbound message queue
} cashier_worker_opt_t;

#endif

