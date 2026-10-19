#!/bin/sh
# Stop the owbox started by tests/headless.sh OUTDIR
out=$1
kill "$(cat "$out/pid")" 2>/dev/null || true
sleep 0.3
kill -9 "$(cat "$out/pid")" 2>/dev/null || true
