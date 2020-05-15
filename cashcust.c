#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

#include "util.h"
#include "cashcust.h"
#include "config.h"
#include "conc_lqueue.h"

volatile sig_atomic_t should_quit = 0;
volatile sig_atomic_t should_close = 0;

long cashier_poll(cashier_opt_t *this) {
    CONC_LQUEUE_ASSERT_EXISTS(this->custqueue);
    long enqueued_customers = -1;
    char *msgbuf = NULL;
    LOG_NEVER("Cashier %d polling...\n", this->id);
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
    if(should_quit) return 1;
    MTX_LOCK_RET(this->state_mtx);
    *(this->state) = state;
    if(extstate != NULL) *extstate = *(this->state);
    COND_SIGNAL_RET(this->state_change_event);
    LOG_DEBUG("Set customer %d state to %d\n", this->id, *(this->state));
    MTX_UNLOCK_RET(this->state_mtx);
    return 0;
}

void cashier_init(cashier_opt_t *c, int id,
                  sigset_t sigset, conc_lqueue_t *outq,
                  long cashier_poll_time, long time_per_prod) {
    c->id = id;
    c->custqueue = conc_lqueue_init(c->custqueue);
    c->outmsgqueue = outq;
    c->state = malloc(sizeof(cashier_state_t));
    c->state_mtx = malloc(sizeof(pthread_mutex_t));
    c->state_change_event = malloc(sizeof(pthread_cond_t));
    c->cashier_poll_time = cashier_poll_time;
    c->time_per_prod = time_per_prod;
    pthread_mutex_init(c->state_mtx, NULL);
    pthread_cond_init(c->state_change_event, NULL);
}

void cashier_destroy(cashier_opt_t *c) {
    conc_lqueue_destroy(c->custqueue);
    free(c->state);
    pthread_mutex_destroy(c->state_mtx);
    free(c->state_mtx);
    pthread_cond_destroy(c->state_change_event);
    free(c->state_change_event);
}

void* cashier_worker(void* arg) {
    cashier_opt_t this = *(cashier_opt_t *) arg;
    customer_opt_t *curr_cust = NULL;
    // Time needed to initially process a customer,
    long start_time,
         poll_time,
         pay_time,
         // Number of enqueued customers
         enqueued_customers;

    CONC_LQUEUE_ASSERT_EXISTS(this.custqueue);

    // ========== Data Initialization ==========

    srand(time(NULL));
    if(pthread_sigmask(SIG_BLOCK, &this.sigset, NULL) < 0) {
        ERR("Masking signals\n");
        goto cashier_worker_exit;
    }

    start_time = RAND_RANGE(CASHIER_START_TIME_MIN,
                            CASHIER_START_TIME_MAX); 
    poll_time = this.cashier_poll_time;

    // ========== Main loop ==========

    while(1) {
        LOG_DEBUG("Cashier %d started\n", this.id);
        if(should_quit)  {
            goto cashier_worker_exit;
        } else if(should_close) {
            MTX_LOCK_EXT(this.state_mtx);
            *(this.state) = SHUTDOWN;
            MTX_UNLOCK_EXT(this.state_mtx);
        }

        MTX_LOCK_EXT(this.state_mtx);
        while(this.state == CLOSED) {
            COND_WAIT_EXT(this.state_change_event, this.state_mtx);
        }
        MTX_UNLOCK_EXT(this.state_mtx);

        if(conc_lqueue_dequeue_nonblock(this.custqueue, 
                                        (void *)&curr_cust) == 0) {
            customer_set_state(curr_cust, PAYING, NULL);

            pay_time = start_time + (curr_cust->products * 
                this.time_per_prod);

            if(pay_time > poll_time) {
                for(int i = 0; i < pay_time / poll_time; i++) {
                    // Number of polls to do WHILE paying
                    enqueued_customers = cashier_poll(&this);
                    msleep(poll_time);
                }
                // Sleep for the remaining pay time
                msleep(pay_time % poll_time);
                customer_set_state(curr_cust, TERMINATED, NULL);
            } else {
                // pay_time < poll_time
                enqueued_customers = cashier_poll(&this);
                msleep(pay_time);
                customer_set_state(curr_cust, TERMINATED, NULL);
                msleep(poll_time - pay_time);
            }
        } else {
            // No customer is enqueued
            enqueued_customers = cashier_poll(&this);
            
            MTX_LOCK_EXT(this.state_mtx);
            if(*(this.state) == SHUTDOWN) {
                // If the supermarket is gently shutting down, exit the thread
                // when no more customers are in line (happens on SIGHUP)
                MTX_UNLOCK_EXT(this.state_mtx);
                LOG_DEBUG("Cashier %d shutting down...\n", this.id);
                goto cashier_worker_exit;
            }
            MTX_UNLOCK_EXT(this.state_mtx);
            msleep(poll_time);
        } 
    }

cashier_worker_exit:
    pthread_exit(NULL);
}

void customer_init(customer_opt_t *c, int id,
                   sigset_t sigset,
                   int *customer_count,
                   pthread_mutex_t *customer_count_mtx,
                   cashier_opt_t *cashier_arr,
                   bool *customer_terminated,
                   long max_shopping_time, 
                   int product_cap,
                   size_t cashier_arr_size) {
    c->id = id;
    c->buying_time = RAND_RANGE(10, max_shopping_time);
    c->products = RAND_RANGE(0, product_cap);
    c->schedule_cond = malloc(sizeof(pthread_cond_t));
    c->state_mtx = malloc(sizeof(pthread_mutex_t));
    c->state_change_event = malloc(sizeof(pthread_cond_t)); 
    c->state = malloc(sizeof(customer_state_t));
    *(c->state) = WAIT_BUY;
    c->customer_count = customer_count;
    c->customer_terminated = customer_terminated;
    c->customer_count_mtx = customer_count_mtx;
    c->cashier_arr = cashier_arr;
    c->cashier_arr_size = cashier_arr_size;
    pthread_cond_init(c->schedule_cond, NULL);
    pthread_mutex_init(c->state_mtx, NULL);
    pthread_cond_init(c->state_change_event, NULL);
    return;
} 

void customer_destroy(customer_opt_t *c) {
    pthread_cond_destroy(c->schedule_cond);
    pthread_cond_destroy(c->state_change_event);
    pthread_mutex_destroy(c->state_mtx);
    free(c->state);
    free(c->state_mtx);
    free(c->state_change_event);
    free(c->schedule_cond);
}

void* customer_worker(void* arg) {
    customer_opt_t this = *(customer_opt_t *) arg;
    customer_state_t state = WAIT_BUY;
    size_t min_queue_id = 0;
    long min_queue_size = -1, curr_size = 0;
    // ========== Initialization ==========

    srand(time(NULL));
    if(pthread_sigmask(SIG_BLOCK, &this.sigset, NULL) < 0) {
        ERR("Masking signals\n");
        goto customer_worker_exit;
    }

    // ========== Shopping ==========

    LOG_DEBUG("Customer %d is shopping...\n", this.id);
    customer_set_state(&this, BUY, &state);
    msleep(this.buying_time);

    if (this.products == 0) {
        customer_set_state(&this, TERMINATED, &state);
        goto customer_worker_wait_confirm;
    }
    
    // ========== Wait for an open cashier and enqueue ==========

    while(state != WAIT_PAY) {
        // TODO: move the scheduling to a different function
        LOG_DEBUG("Customer %d is looking for a cashier...\n", this.id);

        if (should_quit) goto customer_worker_exit;

        // Find the smallest queue
        for(size_t i = 0; i < this.cashier_arr_size; i++) {
            MTX_LOCK_EXT(this.cashier_arr[i].state_mtx);
            if(*(this.cashier_arr[i].state) == OPEN) {
                curr_size = conc_lqueue_getsize(this.cashier_arr[i].custqueue);
                if(min_queue_size <= 0 || curr_size < min_queue_size) { 
                    min_queue_id = i;
                    min_queue_size = curr_size;
                }
            }
            MTX_UNLOCK_EXT(this.cashier_arr[i].state_mtx);
        }

        if(min_queue_size >= 0) {
            LOG_DEBUG("Enqueueing customer %d to cashier %zu",
                this.id, min_queue_id);
            conc_lqueue_enqueue(this.cashier_arr[min_queue_id].custqueue, arg);
            customer_set_state(&this, WAIT_PAY, &state);
            break;
        } 
    }

    // ========== After enqueueing, wait until cashier has finished ==========
    if (should_quit) goto customer_worker_exit;

    LOG_DEBUG("Customer %d is in queue...\n", this.id);
    MTX_LOCK_EXT(this.state_mtx);
    while(*(this.state) != PAYING) {
        if(should_quit) {
            MTX_UNLOCK_EXT(this.state_mtx);
            goto customer_worker_exit;
        }

        COND_WAIT_EXT(this.state_change_event, this.state_mtx);
    }
    MTX_UNLOCK_EXT(this.state_mtx);
    
    LOG_DEBUG("Customer %d is paying...\n", this.id);
    MTX_LOCK_EXT(this.state_mtx);
    while(*(this.state) != TERMINATED) {
        if(should_quit) {
            MTX_UNLOCK_EXT(this.state_mtx);
            goto customer_worker_exit;
        }

        COND_WAIT_EXT(this.state_change_event, this.state_mtx);
    }
    MTX_UNLOCK_EXT(this.state_mtx);

    LOG_DEBUG("Customer %d is waiting for exit confirmation\n", this.id);

customer_worker_wait_confirm:
    // TODO after paying ask manager for out
customer_worker_exit:
    MTX_LOCK_EXT(this.customer_count_mtx);
    *(this.customer_count) = *(this.customer_count) - 1;
    *(this.customer_terminated) = true;
    MTX_UNLOCK_EXT(this.customer_count_mtx);

    LOG_DEBUG("Customer %d has exited\n", this.id);
    pthread_exit(NULL);
}
