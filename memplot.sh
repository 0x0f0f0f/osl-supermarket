#!/bin/env bash

# https://stackoverflow.com/questions/7998302/graphing-a-processs-memory-usage

if [ -z "$1" ]; then
    echo "Missing program" 1>&2;
    exit 1;
fi

# Run the program and get the pidi
"$1" &
PROGPID="$!"
if [ -z "$PROGPID" ]; then
    echo "Program PID not found" 1>&2;
    exit 1
fi

echo "Program $1 PID is $PROGPID"

# trap ctrl-c and call ctrl_c()
trap ctrl_c INT

LOG=$(mktemp)
SCRIPT=$(mktemp)
IMAGE=$(mktemp --suffix=".png")
TEX="./$(basename $1)-memory.tex"
SECS=0.2

echo "Output to LOG=$LOG and SCRIPT=$SCRIPT and IMAGE=$IMAGE"

cat >$SCRIPT <<EOL
set term png small size 800,600
set output "$IMAGE"
set ylabel "RSS"
set key right bottom
set y2label "VSZ"
set ytics nomirror
set y2tics nomirror in
set title "Memory usage graph of process $1, PID $PROGPID"
set yrange [0:*]
set y2range [0:*]
set xlabel "Samples of $SECS seconds"
set style line 1 linecolor rgb '#0060ad' linetype 1 linewidth 2
set style line 2 linecolor rgb '#dd181f' linetype 1 linewidth 2
plot "$LOG" using 3 with lines axes x1y1 title "RSS", "$LOG" using 2 with lines axes x1y2 title "VSZ"
set term latex
set output "$TEX"
plot "$LOG" using 3 with lines lt 1 axes x1y1 title "RSS", "$LOG" using 2 with lines lt 4 axes x1y2 title "VSZ"
EOL


function ctrl_c() {
	gnuplot $SCRIPT
	feh $IMAGE
	exit 0;
}

while true; do
ps -p $PROGPID -o pid= -o vsz= -o rss= | tee -a $LOG
done

