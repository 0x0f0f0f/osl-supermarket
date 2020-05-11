#include <pthread.h>
#include "cashier.h"

#include "conc_lqueue.h"

// ========== Data Types ==========

// Data type for cashier thread.
typedef struct cashier_worker_opt_s {
    int id;
    // 20-80ms
    useconds_t start_time;
    useconds_t time_per_product;
    // Concurrent customer queue
    conc_lqueue_t *custqueue;
    // Outbound message queue
    conc_lqueue_t *outmsgqueue;
    // Inbound message queue
} cashier_worker_opt_t;

void* cashier_worker(void* arg) {
    cashier_opt_t opt = *(cashier_opt_t *) arg;
   
    pthread_exit(NULL);
}
