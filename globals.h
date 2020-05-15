#ifndef globals_h_INCLUDED
#define globals_h_INCLUDED

#include <pthread.h>
#include <signal.h>

extern volatile sig_atomic_t should_quit;
extern volatile sig_atomic_t should_close;
extern volatile sig_atomic_t server_connected;

#endif // globals_h_INCLUDED

