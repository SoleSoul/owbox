#!/bin/sh
# Run owbox under gdb, printing a backtrace if it crashes.  For the tests:
#   OWBOX=tests/gdb.sh tests/headless.sh OUTDIR
# (the backtrace ends up in OUTDIR/owbox.log)
here=$(cd "$(dirname "$0")" && pwd)
exec gdb -q -batch -ex run -ex bt -ex quit --args "$here/../build/owbox" "$@"
