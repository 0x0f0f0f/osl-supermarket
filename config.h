#ifndef config_h_INCLUDED
#define config_h_INCLUDED

// Maximum size of UNIX socket path
#define UNIX_MAX_PATH 108

#define DEFAULT_SOCK_PATH "./orders.sock"
#define DEFAULT_CONFIG_PATH "./config.ini"
#define DEFAULT_MAX_CONN_ATTEMPTS 10
#define DEFAULT_CONN_ATTEMPT_DELAY 500
#define DEFAULT_NUM_CASHIERS 2
#define DEFAULT_CUST_CAP 20 
#define DEFAULT_CUST_BATCH 5
#define DEFAULT_CASHIER_POLL_TIME 80
#define DEFAULT_TIME_PER_PROD 4
#define DEFAULT_MAX_SHOPPING_TIME 500
#define DEFAULT_PRODUCT_CAP 80 
#define DEFAULT_SUPERMARKET_POLL_TIME 10

// Number of cashiers with <= 1 enqueued customer
// necessary to close a cash register
#define DEFAULT_UNDERCROWDED_CASH_TRESHOLD 2
// Number of customers enqueued to a single cashier
// necessary to open another one
#define DEFAULT_OVERCROWDED_CASH_TRESHOLD 10

#define CASHIER_START_TIME_MIN 20
#define CASHIER_START_TIME_MAX 80


#define MANAGER_POOL_SIZE 2

// ========== IPC-protocol messages ==========

// Messages have a fixed size. Should be zero padded and NULL terminated.
#define MSG_SIZE 1024
// Must be followed by the ID of the cashier ended by newline
#define MSG_OPEN_CASH "open_cashier"
#define MSG_CLOSE_CASH "close_cashier"

// This is the first message a supermarket sends to connect to the manager
// Must be followed by the PID of the supermarket process ended by newline
#define HELLO_BOSS "hello_boss\n"
// Sent by server to client on conn established
#define MSG_CONN_ESTABLISHED "conn_established\n"
#define MSG_CASH_HEADER "cash"
#define MSG_QUEUE_SIZE "queue_size"
#define MSG_CUST_HEADER "cust"
// Request when customer wants to exit
#define MSG_WANT_OUT "want_out"
// Customer exit confirmation
#define MSG_GET_OUT "get_out"

#endif // config_h_INCLUDED

