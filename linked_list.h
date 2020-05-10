#ifndef _LINKED_LIST_H
#define _LINKED_LIST_H

#include <stddef.h>

typedef struct _node {
    void* val;
    struct _node *next;
} node_t;

node_t* list_insert_head(node_t* head, void* val);

node_t* list_insert_tail(node_t* head, void* val);

/* Need to know the tail element before! */
int list_insert_tail_fast(node_t* tail, void* val);

/* Returns NULL on failure */
node_t* list_remove_head(node_t* head);


size_t list_length(node_t* head);
void list_free(node_t* head);
void list_map(node_t* head, void fun(void*));

#endif
