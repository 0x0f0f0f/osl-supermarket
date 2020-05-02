#ifndef config_h_INCLUDED
#define config_h_INCLUDED

/** 
 * @brief Maximum size of UNIX socket path
 */
#define UNIX_MAX_PATH 108

// The default socket file
#define DEFAULT_SOCK_PATH "./orders.sock"

// The number of cashier threads (K)
#ifndef NUM_CASHIERS
#define NUM_CASHIERS 2
#endif

// Max number of customers allowed (C)
#ifndef CUSTOMER_CAPACITY
#define CUSTOMER_CAPACITY 20
#endif

// Batch number of customers to be allowed in (E)
#ifndef CUSTOMER_BATCH 
#define CUSTOMER_BATCH 5
#endif

// Maximum time in ms for a customer to shop (T)
#ifndef MAX_SHOPPING_TIME 
#define MAX_SHOPPING_TIME 500
#endif

// Maximum number of products that a customer is allowed to buy (P)
#ifndef PRODUCT_CAPACITY 
#define PRODUCT_CAPACITY 80
#endif

// Polling time for cashier scheduling algorithm
#ifndef SCHEDULE_POLL_TIME
#define SCHEDULE_POLL_TIME 30
#endif

#endif // config_h_INCLUDED

