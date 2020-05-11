#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "conc_lqueue.h"
#include "lqueue.h"
#include "logger.h"
#include "util.h"

int conc_lqueue_enqueue(conc_lqueue_t* cq, void* val) {
    int err = 0;
    MTX_LOCK_RET(cq->mutex);
    if((err = lqueue_enqueue(cq->q, val)) != 0) {
        /* Could not enqueue */
        LOG_DEBUG("error in concurrent enqueue %p: %d\n", (void*) cq, err);
        MTX_UNLOCK_RET(cq->mutex);
        return err;
    }
    LOG_NEVER("successfuly put element %p\n", (void*) val);
    COND_SIGNAL_RET(cq->produce_event);
    MTX_UNLOCK_RET(cq->mutex);
    LOG_NEVER("unlocked after enqueue\n");
    return err;
}

int conc_lqueue_dequeue(conc_lqueue_t* cq, void** val) {
    int err = 0;
    MTX_LOCK_RET(cq->mutex);
    while((err = lqueue_dequeue(cq->q, val)) < 0) {
        /* Queue is empty, wait for value or check if closed */
        if(LQUEUE_CLOSED(cq->q)) {
            LOG_DEBUG("buf %p was closed\n", (void*) cq);
            MTX_UNLOCK_RET(cq->mutex);
            return ELQUEUECLOSED;
        } else if(err != -1) {
            LOG_CRITICAL("error in dequeue: %s", strerror(err));
            MTX_UNLOCK_RET(cq->mutex);
            return err;
        }
        /* Otherwise wait for signal */
        COND_WAIT_RET(cq->produce_event, cq->mutex);
    }
    MTX_UNLOCK_RET(cq->mutex);

    LOG_DEBUG("successfully popped element %p\n", *val);
    return err;
}

int conc_lqueue_closed(conc_lqueue_t* cq) {
    int err = 0;
    MTX_LOCK_RET(cq->mutex);
    err = LQUEUE_CLOSED(cq->q);
    COND_SIGNAL_RET(cq->produce_event);
    MTX_UNLOCK_RET(cq->mutex);
    return err;
}

int conc_lqueue_close(conc_lqueue_t* cq) {
    int err = 0;
    MTX_LOCK_RET(cq->mutex);
    LQUEUE_CLOSE(cq->q);
    COND_SIGNAL_RET(cq->produce_event);
    MTX_UNLOCK_RET(cq->mutex);
    return err;
}

conc_lqueue_t* conc_lqueue_init() {
    conc_lqueue_t* cq = malloc(sizeof(conc_lqueue_t));
    if(cq == NULL) return cq;
    cq->q = lqueue_init();
    if(cq->q == NULL) return NULL;

    cq->mutex = malloc(sizeof(pthread_mutex_t));
    cq->produce_event = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(cq->mutex, NULL);
    pthread_cond_init(cq->produce_event, NULL);

    return cq;
}

void conc_lqueue_destroy(conc_lqueue_t* cq) {
    if (cq == NULL) return;
    if(cq->mutex) pthread_mutex_destroy(cq->mutex);
    if(cq->produce_event) pthread_cond_destroy(cq->produce_event);
    free(cq->mutex);
    free(cq->produce_event);
    lqueue_destroy(cq->q);
    free(cq);
    return;
}
