#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "util.h"
#include "customer.h"
#include "config.h"

// ========== Data Types  ==========

typedef enum {
    BUY,  // 0 - Buying products, waiting for buying_time to elapse. 
    WAIT_PAY,  // 1 - Enqueued in a cashier's line
    PAYING,    // 2 - Being processed by cashier
    TERMINATED // 3 - Wating to be cleaned up 
} customer_state_t;

// This is the data structure that a customer thread
// receives in input from the supermarket process.
// After initialization, a customer waits for buying_time milliseconds
// and then pushes this structure onto a FIFO queue handled by a 
// cashier thread. If a customer buys 0 products, it must inform
// the manager process on exit instead of enqueueing.
// The customer thread follows a state machine model
typedef struct customer_opt_s {
    int id;
    // Time available for a customr thread >10 ms
    useconds_t buying_time;
    int products;
    // Wait on this condition variable until a reschedule event is sent
    pthread_cond_t *schedule_cond;
    pthread_mutex_t *state_mtx;
    customer_state_t *state;
} customer_opt_t;

void* customer_worker(void* arg) {
    customer_opt_t opt = *(customer_opt_t *) arg;
    srandom(time(NULL));

    // Do some shopping
    LOG_DEBUG("Customer %d is shopping...\n", opt.id);
    MTX_LOCK_EXT(opt.state_mtx);
    *(opt.state) = BUY;
    MTX_UNLOCK_EXT(opt.state_mtx);
    msleep(RAND_RANGE(10, MAX_SHOPPING_TIME));

    MTX_LOCK_EXT(opt.state_mtx);
    *(opt.state) = WAIT_PAY;
    MTX_UNLOCK_EXT(opt.state_mtx);

}
