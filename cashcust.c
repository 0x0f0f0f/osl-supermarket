#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "util.h"
#include "cashcust.h"
#include "config.h"
#include "conc_lqueue.h"

void* cashier_worker(void* arg) {
    cashier_opt_t opt = *(cashier_opt_t *) arg;
    cashier_state_t state = CLOSED;
    customer_opt_t *current_cust = NULL;
    char *msgbuf = NULL;
    // Time needed to initially process a customer,
    long start_time,
         poll_time,
         pay_time,
         // Number of enqueued customers
         enqueued_customers;

    // ========== Data Initialization ==========

    srandom(time(NULL));
    start_time = RAND_RANGE(20, 80); 
    poll_time = CASHIER_POLL_TIME;

    // ========== Main loop ==========

    while(1) {
        MTX_LOCK_EXT(opt.state_mtx);
        state = *opt.state;
        while(state != OPEN) {
            COND_WAIT_EXT(opt.state_change_event, opt.state_mtx);
        }
        MTX_UNLOCK_EXT(opt.state_mtx);

        if(conc_lqueue_dequeue(opt.custqueue, (void*) &current_cust) == 0) {
            MTX_LOCK_EXT(current_cust->state_mtx);
            *(current_cust->state) = PAYING;
            COND_SIGNAL_EXT(current_cust->state_change_event);
            MTX_UNLOCK_EXT(current_cust->state_mtx);

            pay_time = start_time + (current_cust->products * TIME_PER_PROD);

            if(pay_time > poll_time) {
                for(int i = 0; i < pay_time / poll_time; i++) {
                    // Number of polls to do WHILE paying
                    enqueued_customers = conc_lqueue_getsize(opt.custqueue);
                    msgbuf = malloc(MSG_SIZE);
                    snprintf(msgbuf, MSG_SIZE, "CASH %d QUEUE_SIZE %ld\n",
                        opt.id, enqueued_customers);
                    conc_lqueue_enqueue(opt.outmsgqueue, (void*) msgbuf;
                    msleep(poll_time);
                }
                // Sleep for the remaining pay time
                msleep(pay_time % poll_time);
            } else {
                // pay_time < poll_time
                sleep(pay_time);
            }
        } else {
            // 
        } 
    }

cashier_worker_exit:
    pthread_exit(NULL);
}

void* customer_worker(void* arg) {
    customer_opt_t opt = *(customer_opt_t *) arg;
    customer_state_t state = WAIT_BUY;
    
    // ========== Data initialization ==========

    srandom(time(NULL));

    // ========== Shopping ==========

    LOG_DEBUG("Customer %d is shopping...\n", opt.id);
    MTX_LOCK_EXT(opt.state_mtx);
    *(opt.state) = BUY;
    state = *(opt.state);
    MTX_UNLOCK_EXT(opt.state_mtx);
    msleep(opt.buying_time);

    // ========== Wait for an open cashier and enqueue ==========

    while(state != WAIT_PAY) {
        for(int i = 0; i < NUM_CASHIERS; i++) {
            MTX_LOCK_EXT(opt.cashier_queue_arr[i].state_mtx);
            if(*(opt.cashier_queue_arr[i].state) == OPEN) {
                MTX_UNLOCK_EXT(opt.cashier_queue_arr[i].state_mtx);
                conc_lqueue_enqueue(opt.cashier_queue_arr[i].custqueue, arg);
                MTX_LOCK_EXT(opt.state_mtx);
                *(opt.state) = WAIT_PAY;
                state = *(opt.state);
                MTX_UNLOCK_EXT(opt.state_mtx);
                break;
            }
            MTX_UNLOCK_EXT(opt.cashier_queue_arr[i].state_mtx);
        }
    }

    // ========== After enqueueing, wait until the cashier has finished ==========

    LOG_DEBUG("Customer %d is in queue...\n", opt.id);
    MTX_LOCK_EXT(opt.state_mtx);
    while(state != PAYING) {
        COND_WAIT_EXT(opt.state_change_event, opt.state_mtx);
        state = *(opt.state);
    }
    MTX_UNLOCK_EXT(opt.state_mtx);

    LOG_DEBUG("Customer %d is paying...\n", opt.id);
    MTX_LOCK_EXT(opt.state_mtx);
    while(state != TERMINATED) {
        COND_WAIT_EXT(opt.state_change_event, opt.state_mtx);
        state = *(opt.state);
    }
    MTX_UNLOCK_EXT(opt.state_mtx);


    // TODO after paying ask manager for out
customer_worker_exit:
    MTX_LOCK_EXT(opt.customer_count_mtx);
    *(opt.customer_count) = *(opt.customer_count) - 1;
    MTX_UNLOCK_EXT(opt.customer_count_mtx);
    pthread_exit(NULL);
}
