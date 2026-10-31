#!/bin/sh
# Stop the owbox started by tests/headless.sh OUTDIR
out=$1
pid=$(cat "$out/pid")
kill "$pid" 2>/dev/null || true
# wait for it to exit (a sanitizer build checks for leaks first)
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    kill -0 "$pid" 2>/dev/null || exit 0
    sleep 0.25
done
kill -9 "$pid" 2>/dev/null || true
