#ifndef config_h_INCLUDED
#define config_h_INCLUDED

// Maximum size of UNIX socket path
#define UNIX_MAX_PATH 108

// The default socket file
#define DEFAULT_SOCK_PATH "./orders.sock"

// The number of cashier threads (K)
#ifndef NUM_CASHIERS
#define NUM_CASHIERS 2
#endif

// Max number of customers allowed (C)
#ifndef CUST_CAP
#define CUST_CAP 20
#endif

// Batch number of customers to be allowed in (E)
#ifndef CUST_BATCH 
#define CUST_BATCH 5
#endif

// Maximum time in ms for a customer to shop (T)
#ifndef MAX_SHOPPING_TIME 
#define MAX_SHOPPING_TIME 500
#endif

// Maximum number of products that a customer is allowed to buy (P)
#ifndef PRODUCT_CAP 
#define PRODUCT_CAP 80
#endif

// Polling time for cashier scheduling algorithm
#ifndef SCHED_POLL_TIME
#define SCHED_POLL_TIME 30
#endif

// ========== IPC-protocol messages ==========
// Messages have a fixed size. Should be zero padded and NULL terminated.
#define MSG_SIZE 512
// Must be followed by the ID of the cashier ended by newline
#define MSG_OPEN_CASH "open_cashier\n"
#define MSG_CLOSE_CASH "close_cashier\n"
// This is the first message a supermarket sends to connect to the manager
// Must be followed by the PID of the supermarket process ended by newline
#define HELLO_BOSS "hello_boss\n"
#endif // config_h_INCLUDED

