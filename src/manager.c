#include <stdlib.h> 
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "config.h"

/**
 * @brief Forward signal to supermarket process. Brutal exit.
 */ 
void handle_sigquit (int sig) {
    // TODO Forward to supermarket process. Brutal exit
    return;
}

void handle_sighup (int sig) {
    // TODO Forward to supermarket process. Gentle exit
    // Wait for consumers to finish buying and shutdown
    return;
}


int main(int argc, char const* argv[]) {
    int fd_sock, err = 0;
    struct sockaddr_un addr;

    // Init unix socket
    memset(&addr, 0, sizeof(addr));

    // After successful socket connection, bind cleanup and
    // signal handlers.


    
    printf("hello world!\n");
}
