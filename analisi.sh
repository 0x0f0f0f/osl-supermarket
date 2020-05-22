#!/bin/bash

if [ -z "$1" ]; then
    echo "usage: analisi.sh LOGFILE" 1>&2;
    exit 1;
fi

if [ ! -f "$1" ]; then
    echo "please specify a regular file" 1>&2;
    exit 1;
fi

for i in $(cat "$1" | grep -E "^customer" | cut -d' ' -f2 | sort |  uniq); do
    CUSTOMER_STATS=$(cat "$1" | grep -E "^customer $i " | cut -d' ' -f3,4)
    # echo $CUSTOMER_STATS
    PRODUCTS_BOUGHT=$(echo $CUSTOMER_STATS | \
        sed -n -e 's/.*products_bought \([[:alnum:]\.]\+\).*/\1/p')
    MS_IN_SUPERMARKET=$(echo $CUSTOMER_STATS | \
        sed -n -e 's/.*ms_in_supermarket \([[:alnum:]\.]\+\).*/\1/p')
    MS_IN_QUEUE=$(echo $CUSTOMER_STATS | \
        sed -n -e 's/.*ms_in_queue \([[:alnum:]\.]\+\).*/\1/p')
    VISITED_QUEUES=$(echo $CUSTOMER_STATS | \
        sed -n -e 's/.*requeue_count \([[:alnum:]\.]\+\).*/\1/p')

    echo "customer $i $PRODUCTS_BOUGHT $MS_IN_SUPERMARKET $MS_IN_QUEUE $(echo $VISITED_QUEUES + 1 | bc)"
done

for i in $(cat "$1" | grep -E "^cashier" | cut -d' ' -f2 | sort | uniq); do
    CASHIER_STATS=$(cat "$1" | grep -E "^cashier $i " | cut -d' ' -f3,4,5,6 )
    # echo $CASHIER_STATS

    SERVICE_TIMES=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($5 == "service_time") print $6}' )
    SUM=$(echo $SERVICE_TIMES | tr ' ' '+' | bc)
    COUNT=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($3 == "customers_served") print $4}' )
    AVG_TIME=$(echo "scale = 3; $SUM / $COUNT" | bc)

    PRODUCTS=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($3 == "products_elaborated") print $4}' )
    TOTAL_PRODUCTS=$(echo $PRODUCTS | tr ' ' '+' | bc)

    CUSTS=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($3 == "customers_served") print $4}' )
    TOTAL_CUSTS=$(echo $CUSTS | tr ' ' '+' | bc)

    OPEN_TIMES=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($3 == "open_for") print $4}' )
    TOTAL_TIME=$(echo $OPEN_TIMES| tr ' ' '+' | bc)

    TIMES_CLOSED=$(cat "$1" | grep -E "^cashier $i " |
        awk '{if ($3 == "times_closed") print $4}' )

    echo "cashier $i $TOTAL_PRODUCTS $TOTAL_CUSTS $TOTAL_TIME $AVG_TIME"
done



