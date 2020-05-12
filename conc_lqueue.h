#ifndef _CONC_LQUEUE_H
#define _CONC_LQUEUE_H

#include <pthread.h>
#include "lqueue.h"

typedef struct __conc_lqueue {
    pthread_mutex_t* mutex;
    pthread_cond_t* produce_event;
    lqueue_t* q;
} conc_lqueue_t;

/* Error code for closed buffer */
#define ELQUEUECLOSED -666

#define CONC_LQUEUE_ASSERT_EXISTS(q) if(q == NULL) \
    {ERR_DIE("expected a queue to be allocated: %p\n", (void*) q);}


/* Put an item in the queue, doing appropriate locking and signaling */
int conc_lqueue_enqueue(conc_lqueue_t* cq, void* val);

/* Get an item from the queue doing appropriate locking and signaling */
int conc_lqueue_dequeue(conc_lqueue_t* cq, void** val);

/* Is a concurrent queue closed? */
int conc_lqueue_closed(conc_lqueue_t* cq);

/* Close a concurrent queue */
int conc_lqueue_close(conc_lqueue_t* cq);

/* Get the number of elements enqueued */
long conc_lqueue_getsize(conc_lqueue_t* cq);

/* Returns NULL on failure */
conc_lqueue_t* conc_lqueue_init();

void conc_lqueue_destroy(conc_lqueue_t* cq);

#endif
