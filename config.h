#ifndef config_h_INCLUDED
#define config_h_INCLUDED

// ========== Parameters  ==========

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

// Polling time for cashier scheduling algorithm (S)
#ifndef SCHED_POLL_TIME
#define SCHED_POLL_TIME 30
#endif

// Maximum connections to be accepted by the manager process
#ifndef MANAGER_POOL_SIZE
#define MANAGER_POOL_SIZE 2
#endif 

// Maximum connection attempts (supermarket process)
#ifndef MAX_CONN_ATTEMPTS 
#define MAX_CONN_ATTEMPTS 10
#endif

// Delay in milliseconds between each connection attempt
#ifndef CONN_ATTEMPT_DELAY 
#define CONN_ATTEMPT_DELAY 500
#endif

// Number customers enqueued to a single cashier
// necessary to open a cashier (S2)
#ifndef UNDERCROWDED_CASH_TRESHOLD
#define UNDERCROWDED_CASH_TRESHOLD 2
#endif

// Number of cashiers with <= 1 customers enqueued
// necessary to close a cashier (S1)
#ifndef OVERCROWDED_CASH_TRESHOLD
#define OVERCROWDED_CASH_TRESHOLD 10
#endif

// Regular time interval when cashier queue size is polled
// and sent to manager
#ifndef CASHIER_POLL_TIME
#define CASHIER_POLL_TIME 80
#endif

// Time required for the processing of a product
#ifndef TIME_PER_PROD 
#define TIME_PER_PROD 4
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

