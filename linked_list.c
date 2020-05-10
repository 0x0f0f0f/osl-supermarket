#include <stdlib.h>
#include <stddef.h>

#include "linked_list.h"


node_t* list_insert_head(node_t* head, void* val) {
    node_t* new = malloc(sizeof(node_t));
    new->val = val;
    new->next = head;
    return new;
}

node_t* list_insert_tail(node_t* head, void* val) {
    if (head == NULL) {
        head = malloc(sizeof(node_t));
        head->val = val;
        head->next = NULL;
        return head;
    } else {
        node_t* curr = head;
        while(curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = malloc(sizeof(node_t));
        curr->next->val = val;
        curr->next->next = NULL;
        return head;
    }
}

node_t* list_remove_head(node_t* head) {
    if(head == NULL) {
        return NULL;
    }
    node_t* tmp = head;
    head = head->next;
    free(tmp);

    return head;
}

size_t list_length(node_t* head) {
    size_t c = 0;
    while(head != NULL) {
        c++; head = head->next;
    }
    return c;
}

void list_free(node_t* head) {
    node_t* tmp;
    while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp->val);
        free(tmp);
    }
    return;
}

void list_map(node_t* head, void fun(void*)) {
    node_t *curr = head;
    while(curr != NULL) {
        fun(curr->val);
        curr = curr->next;
    }
}
