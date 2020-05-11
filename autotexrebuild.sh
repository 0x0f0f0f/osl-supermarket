while true ; do
    mupdf report.pdf &
    export PROGPID=$(jobs -p "%mupdf")
    inotifywait -e modify report.tex
    kill $PROGPID
    make report
done
