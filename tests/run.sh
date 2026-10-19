#!/bin/sh
# Run a command against the owbox started by tests/headless.sh OUTDIR
#   tests/run.sh OUTDIR command args...
out=$1; shift
WAYLAND_DISPLAY=$(cat "$out/display") exec "$@"
