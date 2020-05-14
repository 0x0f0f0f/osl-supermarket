#ifndef globals_h_INCLUDED
#define globals_h_INCLUDED

#include <pthread.h>

extern pthread_mutex_t flags_mtx;
extern volatile char should_quit;
extern volatile char should_close;

#endif // globals_h_INCLUDED

