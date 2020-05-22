#!/bin/bash

if [ -z "$1" ]; then
    echo "Missing program name" 1>&2
    exit 1
fi

if [ -z "$2" ]; then
    echo "Missing test duration" 1>&2
    exit 1
fi

if [ -z "$3" ]; then
    echo "Missing signal" 1>&2
    exit 1
fi


valgrind --leak-check=full --log-file=./valgrind-manager.log  ./manager -c "$1" &
MANAGER_PID="$!"
valgrind --leak-check=full --log-file=./valgrind-supermarket.log  ./supermarket -c "$1" &
SUPERMARKET_PID="$!"
(sleep "$2" && kill -$3 $MANAGER_PID)

exit 0
