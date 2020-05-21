#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>

#include "util.h"
#include "cashcust.h"
#include "config.h"
#include "conc_lqueue.h"

volatile sig_atomic_t should_quit = 0;
volatile sig_atomic_t should_close = 0;


int customer_set_state(customer_opt_t *this, customer_state_t state) {
                       // customer_state_t *extstate) {
    if(should_quit) return 1;
    MTX_LOCK_RET(this->state_mtx);
    *(this->state) = state;
    // if(extstate != NULL) *extstate = *(this->state);
    COND_SIGNAL_RET(this->state_change_event);
    LOG_DEBUG("Set customer %d state to %d\n", this->id, *(this->state));
    MTX_UNLOCK_RET(this->state_mtx);
    return 0;
}

void cashier_init(cashier_opt_t *c, int id,
                  bool *isopen,
                  pthread_mutex_t *state_mtx,
                  long time_per_prod) {
    c->id = id;
    c->custqueue = conc_lqueue_init(c->custqueue);
    c->isopen = isopen;
    c->state_mtx = state_mtx;
    c->time_per_prod = time_per_prod;
}

void cashier_destroy(cashier_opt_t *c) {
    conc_lqueue_free(c->custqueue);
}

void* cashier_poll_worker(void* arg) {
    cashier_poll_opt_t *this = (cashier_poll_opt_t *) arg;
    bool curr_isopen = false;
    char *msgbuf = NULL;
    char cipher[5] = {0};
    cashier_opt_t *cash = NULL;
    long enqueued_customers = -1;
    
    while(!should_quit) {
        msgbuf = calloc(1, MSG_SIZE);
        snprintf(msgbuf, MSG_SIZE, "%s", MSG_QUEUE_SIZE);

        LOG_DEBUG("Polling...\n");
        // printf("%d \n", this->cashier_arr_size);
        for (int i = 0; i < this->cashier_arr_size; i++) {
            curr_isopen = false;
            cash = &this->cashier_arr[i];
            enqueued_customers = -1;
            MTX_LOCK_EXT(&this->cashier_mtx_arr[i]);
            curr_isopen = this->cashier_isopen_arr[i];
            MTX_UNLOCK_EXT(&this->cashier_mtx_arr[i]);

            if(curr_isopen) {
                // LOG_DEBUG("Polling cashier %d\n", i);
                // CONC_LQUEUE_ASSERT_EXISTS(cash->custqueue);
                enqueued_customers = conc_lqueue_getsize(cash->custqueue);
            }
            snprintf(cipher, 5,
                " %ld", enqueued_customers);

            strncat(msgbuf, cipher, MSG_SIZE);

            if(strlen(msgbuf) == MSG_SIZE) {
                LOG_CRITICAL("Buffer overflow in queue size poll");
                free(msgbuf);
                goto cashier_poll_exit;
            }
        }
        strncat(msgbuf, "\n", MSG_SIZE);

        conc_lqueue_enqueue(this->outmsgqueue, (void*) msgbuf);
        msleep(this->cashier_poll_time);
    }

cashier_poll_exit:
    return (NULL);
}

int cashier_reschedule_enqueued_customers(cashier_opt_t *ca) {
    int err = 0;
    MTX_LOCK_RET(ca->custqueue->mutex);
    long size = ca->custqueue->q->count;

    if (size > 0) {
        for(long i = 0; i < size; i++) {
           // TODO is it ok to reschedule on a probability?
           if (RAND_RANGE(0, 3) == 0) {
               customer_opt_t *cu = NULL;
               err = lqueue_remove_index(ca->custqueue->q, (void*) &cu, i);

               // printf("%d\n", err);
               if(err == 0) {
                   LOG_DEBUG("rescheduling customer %d\n", cu->id);
                   MTX_UNLOCK_RET(ca->custqueue->mutex);
                   customer_reschedule(cu);
               }
               else {
                   MTX_UNLOCK_RET(ca->custqueue->mutex);
                   return err;
               }
           }
        }
    }
    MTX_UNLOCK_RET(ca->custqueue->mutex);

    return 0;
}

void* customer_renqueue_worker(void *arg) {
    customer_renqueue_worker_t *opt = (customer_renqueue_worker_t*) arg;
    while(!should_quit) {
        for(int i = 0; i < opt->cashier_arr_size; i++) {
            MTX_LOCK_EXT(&opt->cashier_mtx_arr[i]);
            if(opt->cashier_isopen_arr[i] == true) {
                MTX_UNLOCK_EXT(&opt->cashier_mtx_arr[i]);
                // printf("removing and rescheduling customers of cashier %d\n", i);
                cashier_reschedule_enqueued_customers(&opt->cashier_arr[i]);

            } else {
                MTX_UNLOCK_EXT(&opt->cashier_mtx_arr[i]);
            }
        }
        // TODO get S from config
        msleep(80);
    }
    return NULL;
}

void* cashier_worker(void* arg) {
    printf("Cashier worker strating\n");
    cashier_opt_t this = *(cashier_opt_t *) arg;
    customer_opt_t *curr_cust = NULL;
    // Time needed to initially process a customer,
    long start_time,
         pay_time;
         // Number of enqueued customers
    char *msgbuf = NULL;
    int err = 0;

    CONC_LQUEUE_ASSERT_EXISTS(this.custqueue);

    // ========== Initialization ==========

    srand(time(NULL));

    start_time = RAND_RANGE(CASHIER_START_TIME_MIN,
                            CASHIER_START_TIME_MAX); 

    // ========== Main loop ==========

    while(!should_quit) {
        printf("Cashier %d looping \n", this.id);

        MTX_LOCK_EXT(this.state_mtx);
        if(!*(this.isopen)) {
            MTX_UNLOCK_EXT(this.state_mtx);
            goto cashier_worker_exit;
        }
        MTX_UNLOCK_EXT(this.state_mtx);

        if((err = conc_lqueue_dequeue_nonblock(this.custqueue, 
                                        (void *)&curr_cust)) == 0) {
            customer_set_state(curr_cust, PAYING);
            pay_time = start_time + (curr_cust->products * 
                this.time_per_prod);
            msleep(pay_time);
            customer_set_state(curr_cust, TERMINATED);
        } else if(err == ELQUEUEEMPTY) {
            if (should_close) {
                // If the supermarket is gently shutting down, exit the thread
                // when no more customers are in line (happens on SIGHUP)
                LOG_DEBUG("Cashier %d shutting down...\n", this.id);
                goto cashier_worker_exit;
            }
            msleep(start_time);
        } else {
            LOG_CRITICAL("Unknown Error in cashier %d queue", this.id);
            goto cashier_worker_exit_instantly;
        }

    }

cashier_worker_exit:
    printf("Cashier closing\n");
    LOG_DEBUG("Cashier %d has closed\n", this.id);
cashier_worker_exit_instantly:
    return (NULL);
}

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
                   conc_lqueue_t *outmsgqueue) {
    c->id = id;
    c->buying_time = RAND_RANGE(10, max_shopping_time);
    c->products = RAND_RANGE(0, product_cap);
    c->schedule_cond = calloc(1, sizeof(pthread_cond_t));
    c->state_mtx = calloc(1, sizeof(pthread_mutex_t));
    c->state_change_event = calloc(1, sizeof(pthread_cond_t)); 
    c->state = calloc(1, sizeof(customer_state_t));
    *(c->state) = WAIT_BUY;
    c->customer_count = customer_count;
    c->customer_terminated = customer_terminated;
    c->customer_count_mtx = customer_count_mtx;
    c->cashier_arr = cashier_arr;
    c->cashier_isopen_arr = cashier_isopen_arr,
    c->cashier_mtx_arr = cashier_mtx_arr,
    c->cashier_arr_size = cashier_arr_size;
    c->outmsgqueue = outmsgqueue;
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


int customer_reschedule(customer_opt_t *this) {
    if(should_quit) return 1;
    LOG_DEBUG("Scheduling customer %d\n", this->id);
   
    bool rescheduled = false;
    long min_queue_size = INT_MAX, curr_size = 0;
    int min_queue_id = -1;

    while (!rescheduled) {
    for(int i = 0; i < this->cashier_arr_size; i++) {
        MTX_LOCK_EXT(&this->cashier_mtx_arr[i]);
        if(this->cashier_isopen_arr[i]) {
            curr_size = conc_lqueue_getsize(this->cashier_arr[i].custqueue);
            if(min_queue_id < 0 || curr_size < min_queue_size) { 
                min_queue_id = i;
                min_queue_size = curr_size;
            }
        }
        MTX_UNLOCK_EXT(&this->cashier_mtx_arr[i]);
    }

    if(min_queue_id >= 0) {
        LOG_DEBUG("Enqueueing customer %d to cashier %d\n",
            this->id, min_queue_id);
        MTX_LOCK_EXT(&this->cashier_mtx_arr[min_queue_id]);
        if (this->cashier_isopen_arr[min_queue_id] == false) {
            MTX_UNLOCK_EXT(&this->cashier_mtx_arr[min_queue_id]);
            continue;
        }
        conc_lqueue_enqueue(this->cashier_arr[min_queue_id].custqueue,
                            (void*) this);
        customer_set_state(this, WAIT_PAY);
        MTX_UNLOCK_EXT(&this->cashier_mtx_arr[min_queue_id]);
        rescheduled = true;
    }

    //TODO move to constant
    // sleep(2);
    }

    return 0;
}



void* customer_worker(void* arg) {
    customer_opt_t *this = (customer_opt_t *) arg;
    char *msgbuf;
    bool is_enqueued = false;
    // ========== Initialization ==========

    srand(time(NULL));

    // ========== Shopping ==========

    LOG_DEBUG("Customer %d is shopping...\n", this->id);
    customer_set_state(this, BUY);
    msleep(this->buying_time);

    if (this->products == 0) {
        customer_set_state(this, TERMINATED);
        goto customer_worker_wait_confirm;
    }
    
    // ========== Wait for an open cashier and enqueue ==========

    LOG_DEBUG("Customer %d is looking for a cashier...\n", this->id);
    if (should_quit) goto customer_worker_exit;

    customer_reschedule(this);

    // ========== After enqueueing, wait until cashier has finished ==========
    if (should_quit) goto customer_worker_exit;

    LOG_DEBUG("Customer %d is in queue...\n", this->id);
    MTX_LOCK_EXT(this->state_mtx);
    while(*(this->state) != PAYING) {
        if(should_quit) {
            MTX_UNLOCK_EXT(this->state_mtx);
            goto customer_worker_exit;
        }

        COND_WAIT_EXT(this->state_change_event, this->state_mtx);
    }
    MTX_UNLOCK_EXT(this->state_mtx);
    
    LOG_DEBUG("Customer %d is paying...\n", this->id);
    MTX_LOCK_EXT(this->state_mtx);
    while(*(this->state) != TERMINATED) {
        if(should_quit) {
            MTX_UNLOCK_EXT(this->state_mtx);
            goto customer_worker_exit;
        }

        COND_WAIT_EXT(this->state_change_event, this->state_mtx);
    }
    MTX_UNLOCK_EXT(this->state_mtx);

    // ========== Ask manager to get out  ==========

    msgbuf = calloc(1, MSG_SIZE);
    snprintf(msgbuf, MSG_SIZE, "%s %d %s\n",
             MSG_CUST_HEADER, this->id, MSG_WANT_OUT);
    if (conc_lqueue_enqueue(this->outmsgqueue, (void*) msgbuf) != 0) {
        ERR("Error enqueueing message: %s", msgbuf);
        free(msgbuf);
        goto customer_worker_exit;
    }
    
customer_worker_wait_confirm:
    LOG_DEBUG("Customer %d is waiting for exit confirmation\n", this->id);
    MTX_LOCK_EXT(this->state_mtx);
    while(*(this->state) != CAN_EXIT) {
        if(should_quit || should_close) {
            MTX_UNLOCK_EXT(this->state_mtx);
            goto customer_worker_exit;
        }

        COND_WAIT_EXT(this->state_change_event, this->state_mtx);
    }
    MTX_UNLOCK_EXT(this->state_mtx);

customer_worker_exit:
    MTX_LOCK_EXT(this->customer_count_mtx);
    *(this->customer_count) = *(this->customer_count) - 1;
    *(this->customer_terminated) = true;
    // LOG_DEBUG("Decrementing customer count = %d\n", *(this->customer_count));
    MTX_UNLOCK_EXT(this->customer_count_mtx);

    LOG_DEBUG("Customer %d has exited\n", this->id);
    return (NULL);
}
