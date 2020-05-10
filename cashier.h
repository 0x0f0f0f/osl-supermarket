#ifndef cashier_h_INCLUDED
#define cashier_h_INCLUDED

#include <unistd.h>

#include "conc_lqueue.h"

// ========== Data Types ==========

// Data type for cashier thread.
typedef struct cashier_s {
    // 20-80ms
    useconds_t start_time;
    useconds_t time_per_product;
    // Concurrent queue
    conc_lqueue_t queue;
} cashier_t;

#endif

