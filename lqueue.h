/* Header file containing circular queue functions */
/* Need to define QUEUE_SIZE! */
#ifndef _LIST_QUEUE_H
#define _LIST_QUEUE_H

#include "linked_list.h"

/* Queue types, linked list of infinite capacity */
typedef struct __lqueue_t {
    node_t *head;
    char closed;
    int count;
} lqueue_t;

/* Buffer utility macros and functions */
#define LQUEUE_EMPTY(b) (b->head == NULL)
#define LQUEUE_ASSERT_EXISTS(b) if(b == NULL) {ERR_DIE("expected a buffer to be allocated: %p\n", (void*) b);}
#define LQUEUE_OPEN(b) (b->closed == 0)
#define LQUEUE_CLOSED(b) !LQUEUE_OPEN(b)
#define LQUEUE_CLOSE(b) (b->closed = 1)

/* Returns -1 on failure */
int lqueue_enqueue(lqueue_t* q, void* el);

/* Returns -1 on failure */
int lqueue_dequeue(lqueue_t* q, void** val);

/* Returns NULL on failure */
lqueue_t* lqueue_init();

void lqueue_destroy(lqueue_t* q);

#endif
