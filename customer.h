#ifndef customer_h_INCLUDED
#define customer_h_INCLUDED

#include <unistd.h>
#include <pthread.h> 

// ========== Data Types  ==========

typedef enum {
    WAIT_BUY,  // 0 - Just started, waiting to start buying.
    BUYING,    // 1 - Buying products, waiting for buying_time to elapse. 
    WAIT_PAY,  // 2 - Enqueued in a cashier's line
    PAYING,    // 3 - Being processed by cashier
    TERMINATED // 4 - Wating to be cleaned up 
} customer_state_t;

// This is the data structure that a customer thread
// receives in input from the supermarket process.
// After initialization, a customer waits for buying_time milliseconds
// and then pushes this structure onto a FIFO queue handled by a 
// cashier thread. If a customer buys 0 products, it must inform
// the manager process on exit instead of enqueueing.  
typedef struct customer_s {
    int id;
    // Time available for a customr thread >10 ms
    useconds_t buying_time;
    int products;
    pthread_cond_t schedule_cond;
    pthread_mutex_t state_mtx;
    customer_state_t state;
} customer_t;

// ========== Methods  ==========

#endif // customer_h_INCLUDED

