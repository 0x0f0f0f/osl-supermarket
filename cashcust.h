#ifndef customer_h_INCLUDED
#define customer_h_INCLUDED

#include <unistd.h>
#include <pthread.h> 
#include <stdbool.h>
#include <signal.h>
#include "conc_lqueue.h"

// ========== Cashier Data Types ==========

// Data type for cashier thread.
typedef struct cashier_opt_s {
    int id;
    // Concurrent customer queue
    conc_lqueue_t *custqueue;
    // Cashier state 
    bool *isopen;
    pthread_mutex_t *state_mtx;
    // Various time units
    long time_per_prod;
} cashier_opt_t;

// ========== Customer Data Types ==========

typedef enum {
    WAIT_BUY,   // 0 - Initial State
    BUY,        // 1 - Buying products, waiting for buying_time to elapse. 
    WAIT_PAY,   // 2 - Enqueued in a cashier's line
    PAYING,     // 3 - Being processed by cashier
    TERMINATED, // 4 - Wating to be cleaned up 
    CAN_EXIT    // 5 - Is allowed to leave
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
    long buying_time;
    int products;
    // Wait on this condition variable until a reschedule event is sent
    pthread_cond_t *schedule_cond;
    customer_state_t *state;
    pthread_mutex_t *state_mtx;
    pthread_cond_t *state_change_event;
    // Number of customers in the supermarket
    int *customer_count;
    pthread_mutex_t *customer_count_mtx;
    bool *customer_terminated;
    // Array of cashiers to choose where to enqueue the customer
    cashier_opt_t *cashier_arr;
    bool *cashier_isopen_arr;
    pthread_mutex_t *cashier_mtx_arr;
    int cashier_arr_size;
    conc_lqueue_t *outmsgqueue;
} customer_opt_t;

typedef struct cashier_poll_opt_s {
    cashier_opt_t *cashier_arr;
    pthread_mutex_t *cashier_mtx_arr;
    bool *cashier_isopen_arr;
    int cashier_arr_size;
    long cashier_poll_time;
    conc_lqueue_t *outmsgqueue;

} cashier_poll_opt_t;

typedef struct customer_renqueue_worker_t {
    bool* cashier_isopen_arr;
    pthread_mutex_t *cashier_mtx_arr;
    int cashier_arr_size;
    cashier_opt_t *cashier_arr;
} customer_renqueue_worker_t;

// ========== Worker Function Declarations ==========
void* cashier_poll_worker(void* arg);
void* cashier_worker(void* arg);
void* customer_worker(void* arg);
void cashier_init(cashier_opt_t *c, int id,
                  bool *isopen,
                  pthread_mutex_t *state_mtx,
                  long time_per_prod);

void customer_init(customer_opt_t *c, int id,
                   int *customer_count,
                   pthread_mutex_t *customer_count_mtx,
                   cashier_opt_t *cashier_arr,
                   bool *cashier_isopen_arr,
                   pthread_mutex_t *cashier_mtx_arr,
                   bool *customer_terminated,
                   long max_shopping_time, 
                   int product_cap,
                   int cashier_arr_size,
                   conc_lqueue_t *outmsgqueue
);

void cashier_destroy(cashier_opt_t *c);
void customer_destroy(customer_opt_t *c);
int customer_reschedule(customer_opt_t *this);
int cashier_reschedule_enqueued_customers(cashier_opt_t *ca);
void* customer_renqueue_worker(void *arg);

#endif // customer_h_INCLUDED

