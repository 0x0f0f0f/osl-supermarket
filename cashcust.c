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

long cashier_poll(cashier_opt_t *this) {
    CONC_LQUEUE_ASSERT_EXISTS(this->custqueue);
    long enqueued_customers = -1;
    char *msgbuf = NULL;
    LOG_DEBUG("Cashier %d polling...\n", this->id);
    enqueued_customers = conc_lqueue_getsize(this->custqueue);
    msgbuf = malloc(MSG_SIZE);
    memset(msgbuf, 0, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "CASH %d QUEUE_SIZE %ld\n",
        this->id, enqueued_customers);
    conc_lqueue_enqueue(this->outmsgqueue, (void*) msgbuf);
    return enqueued_customers;
}

int customer_set_state(customer_opt_t *this, customer_state_t state,
                       customer_state_t *extstate) {
    MTX_LOCK_RET(this->state_mtx);
    *(this->state) = state;
    if(extstate != NULL) *extstate = *(this->state);
    COND_SIGNAL_RET(this->state_change_event);
    LOG_DEBUG("Set customer %d state to %d\n", this->id, *(this->state));
    MTX_UNLOCK_RET(this->state_mtx);
    return 0;
}

void cashier_init(cashier_opt_t *c, int id, conc_lqueue_t *outq) {
    c->id = id;
    c->custqueue = conc_lqueue_init(c->custqueue);
    c->outmsgqueue = outq;
    c->state = malloc(sizeof(cashier_state_t));
    c->state_mtx = malloc(sizeof(pthread_mutex_t));
    c->state_change_event = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(c->state_mtx, NULL);
    pthread_cond_init(c->state_change_event, NULL);
}

void* cashier_worker(void* arg) {
    cashier_opt_t this = *(cashier_opt_t *) arg;
    customer_opt_t *current_cust = NULL;
    // Time needed to initially process a customer,
    long start_time,
         poll_time,
         pay_time,
         // Number of enqueued customers
         enqueued_customers;

    CONC_LQUEUE_ASSERT_EXISTS(this.custqueue);
    // ========== Data Initialization ==========

    srand(time(NULL));
    start_time = RAND_RANGE(20, 80); 
    poll_time = CASHIER_POLL_TIME;

    // ========== Main loop ==========

    while(1) {
        MTX_LOCK_EXT(this.state_mtx);
        while(this.state == CLOSED) {
            COND_WAIT_EXT(this.state_change_event, this.state_mtx);
        }
        MTX_UNLOCK_EXT(this.state_mtx);

        if(conc_lqueue_dequeue(this.custqueue, (void *) &current_cust) == 0) {
            customer_set_state(current_cust, PAYING, NULL);

            pay_time = start_time + (current_cust->products * TIME_PER_PROD);

            if(pay_time > poll_time) {
                for(int i = 0; i < pay_time / poll_time; i++) {
                    // Number of polls to do WHILE paying
                    enqueued_customers = cashier_poll(&this);
                    msleep(poll_time);
                }
                // Sleep for the remaining pay time
                msleep(pay_time % poll_time);
                customer_set_state(current_cust, TERMINATED, NULL);
            } else {
                // pay_time < poll_time
                enqueued_customers = cashier_poll(&this);
                sleep(pay_time);
                customer_set_state(current_cust, TERMINATED, NULL);
                sleep(poll_time - pay_time);
            }
        } else {
            // No customer is enqueued
            enqueued_customers = cashier_poll(&this);
            
            MTX_LOCK_EXT(this.state_mtx);
            if(*(this.state) == SHUTDOWN) {
                // If the supermarket is gently shutting down, exit the thread
                // when no more customers are in line (happens on SIGHUP)
                LOG_DEBUG("Cashier %d shutting down...\n", this.id);
                pthread_exit(NULL); 
            }
            MTX_UNLOCK_EXT(this.state_mtx);
            sleep(poll_time);
        } 
    }

    pthread_exit(NULL);
}

void customer_init(customer_opt_t *c, int id, int *customer_count,
                   pthread_mutex_t *customer_count_mtx,
                   cashier_opt_t *cashier_arr) {
    c->id = id;
    c->buying_time = RAND_RANGE(10, MAX_SHOPPING_TIME);
    c->products = RAND_RANGE(0, PRODUCT_CAP);
    c->schedule_cond = malloc(sizeof(pthread_cond_t));
    c->state_mtx = malloc(sizeof(pthread_mutex_t));
    c->state_change_event = malloc(sizeof(pthread_cond_t)); 
    c->state = malloc(sizeof(customer_state_t));
    *(c->state) = WAIT_BUY;
    c->customer_count = customer_count;
    c->customer_count_mtx = customer_count_mtx;
    c->cashier_arr = cashier_arr;
    pthread_cond_init(c->schedule_cond, NULL);
    pthread_mutex_init(c->state_mtx, NULL);
    pthread_cond_init(c->state_change_event, NULL);
    return;
} 

void* customer_worker(void* arg) {
    customer_opt_t this = *(customer_opt_t *) arg;
    customer_state_t state = WAIT_BUY;
    
    // ========== Data initialization ==========

    srand(time(NULL));

    // ========== Shopping ==========

    LOG_DEBUG("Customer %d is shopping...\n", this.id);
    customer_set_state(&this, BUY, &state);
    msleep(this.buying_time);

    // ========== Wait for an open cashier and enqueue ==========

    while(state != WAIT_PAY) {
        for(int i = 0; i < NUM_CASHIERS; i++) {
            MTX_LOCK_EXT(this.cashier_arr[i].state_mtx);
            if(*(this.cashier_arr[i].state) == OPEN) {

                MTX_UNLOCK_EXT(this.cashier_arr[i].state_mtx);
                conc_lqueue_enqueue(this.cashier_arr[i].custqueue, arg);
                customer_set_state(&this, WAIT_PAY, &state);
                break;
            }
            MTX_UNLOCK_EXT(this.cashier_arr[i].state_mtx);
        }
    }

    // ========== After enqueueing, wait until cashier has finished ==========

    LOG_DEBUG("Customer %d is in queue...\n", this.id);
    MTX_LOCK_EXT(this.state_mtx);
    while(*(this.state) != PAYING) {
        COND_WAIT_EXT(this.state_change_event, this.state_mtx);
    }
    MTX_UNLOCK_EXT(this.state_mtx);

    LOG_DEBUG("Customer %d is paying...\n", this.id);
    MTX_LOCK_EXT(this.state_mtx);
    while(*(this.state) != TERMINATED) {
        COND_WAIT_EXT(this.state_change_event, this.state_mtx);
    }
    MTX_UNLOCK_EXT(this.state_mtx);

    LOG_DEBUG("Customer %d is waiting for exit confirmation\n", this.id);

    // TODO after paying ask manager for out
customer_worker_exit:
    MTX_LOCK_EXT(this.customer_count_mtx);
    *(this.customer_count) = *(this.customer_count) - 1;
    MTX_UNLOCK_EXT(this.customer_count_mtx);
    pthread_exit(NULL);
}
