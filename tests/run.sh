#!/bin/sh
# Run a command against the owbox started by tests/headless.sh OUTDIR
#   tests/run.sh OUTDIR command args...
out=$1; shift
if [ -f "$out/xdisplay" ]; then
    DISPLAY=$(cat "$out/xdisplay")
    export DISPLAY
else
    unset DISPLAY
fi
# owbox --msg would talk to the owbox this runs in (when testing inside an
# owbox session) instead of the test one
unset OWBOX_SOCK
WAYLAND_DISPLAY=$(cat "$out/display") exec "$@"
