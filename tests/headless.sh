#!/bin/sh
# Run owbox on wlroots' headless backend for automated testing.
#
#   tests/headless.sh OUTDIR [owbox args...]
#
# Starts owbox in the background, writes its log to OUTDIR/owbox.log, the
# Wayland display name to OUTDIR/display, the X display (when Xwayland is
# enabled) to OUTDIR/xdisplay and its pid to OUTDIR/pid.  Use tests/run.sh to
# run clients (eg grim for screenshots) and tests/stop.sh to stop it.

set -e
out=$1; shift
mkdir -p "$out"
here=$(dirname "$0")
bin=${OWBOX:-$here/../build/owbox}

# the themes in the source tree first (data/themes), so that tests don't
# depend on an installed owbox
XDG_DATA_DIRS="$(cd "$here/../data" && pwd):${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \
WLR_BACKENDS=headless WLR_RENDERER=${WLR_RENDERER:-pixman} \
WLR_HEADLESS_OUTPUTS=${OUTPUTS:-1} WLR_LIBINPUT_NO_DEVICES=1 \
XDG_CURRENT_DESKTOP=owbox \
    "$bin" --debug --no-autostart "$@" > "$out/owbox.log" 2>&1 &
echo $! > "$out/pid"

# wait for the socket
i=0
while [ $i -lt 50 ]; do
    d=$(grep -m1 -o 'WAYLAND_DISPLAY=[a-z0-9-]*' "$out/owbox.log" 2>/dev/null | cut -d= -f2)
    if [ -n "$d" ]; then
        echo "$d" > "$out/display"
        # logged right after the Wayland display
        sleep 0.1
        x=$(grep -m1 -o 'DISPLAY=:[0-9]*' "$out/owbox.log" | cut -d= -f2 || true)
        rm -f "$out/xdisplay"
        [ -n "$x" ] && echo "$x" > "$out/xdisplay"
        exit 0
    fi
    i=$((i + 1))
    sleep 0.1
done
echo "owbox did not start, see $out/owbox.log" >&2
exit 1
