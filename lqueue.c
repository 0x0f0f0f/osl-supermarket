#include <stdio.h>
#include <stdlib.h>
#include "lqueue.h"
#include "linked_list.h"
#include "logger.h"

int lqueue_enqueue(lqueue_t* q, void* el) {
    int err = 0;
    LOG_NEVER("Enqueueing %p in %p\n", el, (void*) q);
    if(LQUEUE_CLOSED(q)) {
        LOG_NEVER("queue %p was closed in enqueue", (void*) q);
        return -2;
    }
    q->count++;

    if((q->head = list_insert_tail(q->head, el)) == NULL) {
        LOG_CRITICAL("Error inserting at tail\n");
        return -1;
    }

    return err;
}

int lqueue_dequeue(lqueue_t* q, void** val) {
    if(LQUEUE_EMPTY(q)) {
        LOG_NEVER("queue %p is empty in dequeue\n", (void*)q);
        return -1;
    }
    *val = (q->head)->val;
    q->head = list_remove_head(q->head);
    q->count--;
    return 0;
}

lqueue_t* lqueue_init () {
    lqueue_t* q = calloc(1, sizeof(lqueue_t));
    if(q == NULL) return q;
    q->head = NULL;
    q->closed = 0;
    q->count = 0;
    return q;
}

void lqueue_destroy(lqueue_t* q) {
    list_destroy(q->head);
    free(q);
    return;
}

void lqueue_free(lqueue_t* q) {
    list_free(q->head);
    free(q);
}


int lqueue_remove_index(lqueue_t* q, void** val, int ind) {
    if (q == NULL) return -2;
    if (ind >= q->count || ind < 0) {
        LOG_CRITICAL("Index out of bounds\n");
        return -1;
    } else if (ind == 0) {
        *val = (q->head)->val;
        q->head = list_remove_head(q->head);
        q->count--;
    } else {
        node_t *prev = q->head;
        for(int i = 0; i < ind - 1; i++) {
            prev = prev->next;
        }
        node_t *cur = prev->next;
        prev->next = cur->next;
        *val = (cur->val);
        q->count--;
        return 0;
    }
    return 0;
}
