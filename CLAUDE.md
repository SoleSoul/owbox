# Working on owbox

owbox is Openbox ported to Wayland on wlroots. Read README.md for what it
is, docs/ARCHITECTURE.md for how it's built (read it before changing code),
docs/DECISIONS.md for why, and docs/ROADMAP.md for what's next.

## Ground rules

- **Openbox's behavior is the specification.** Before changing how
  something behaves, look at what Openbox does (`ref/openbox/openbox/`). Keep
  ported Openbox code recognizable, with the same names, structure and
  comments, so that it can be compared with Openbox's. Replace X calls,
  don't restructure around them.
- **Drop-in compatibility**: an Openbox rc.xml, menu.xml or theme must keep
  working. owbox's settings go in the `<owbox>` section only.
- Match the surrounding code's style: Openbox's C style (4 spaces, glib
  types, `/* */` comments, the emacs mode line and GPL header on each
  file).
- Document at milestones, not mid-work: README.md, docs/*.md, the man page
  (docs/owbox.1), and the comments in data/rc.xml for any new `<owbox>`
  setting.
- End commit messages with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Building

    meson setup build          # once
    ninja -C build

This builds `build/owbox`, `build/owbox-input` (the test input tool) and
`build/owbox-theme-render`. compile_commands.json is symlinked at the top
for clangd. Dependencies are listed in README.md (Arch: wlroots0.20,
libsfdo, librsvg, pango, xcb-util-wm, ...).

The reference sources in `ref/` are gitignored. To recreate them:

    git clone https://github.com/Mikachu/openbox.git ref/openbox
    git clone -b 0.20.2 https://gitlab.freedesktop.org/wlroots/wlroots.git ref/wlroots

## Testing

Run the smoke test after changes:

    tests/smoke.py            # --keep keeps the log and screenshots

For manual testing on the headless backend (no hardware, nothing on the
screen):

    T=/some/scratch/dir
    tests/headless.sh $T [owbox args]   # starts owbox, writes $T/display, $T/xdisplay, $T/pid, $T/owbox.log
    tests/run.sh $T foot &              # run a client in it
    tests/run.sh $T build/owbox --msg windows        # the windows as JSON
    tests/run.sh $T build/owbox-input move 500 108 down left drag 300 300 up left
    tests/run.sh $T build/owbox-input key alt+Tab sleep 200 run "grim $T/shot.png"
    tests/stop.sh $T

- `owbox-input` commands: `move X Y`, `down|up|click|dclick BUTTON`,
  `drag X Y`, `scroll up|down|left|right`, `keydown|keyup KEY`,
  `key alt+F4`, `type TEXT`, `sleep MS`, `run CMD`. Use `run` to take
  screenshots while keys are held: the virtual keyboard is released when
  the tool exits.
- Screenshots: `grim` through `tests/run.sh`. Use `tools/crop.py IN OUT X Y
  W H [SCALE]` to zoom in on decorations.
- Crashes: `OWBOX=tests/gdb.sh tests/headless.sh $T` puts a backtrace in
  `$T/owbox.log` (`grep -A15 "received signal"`).
- X applications: `GDK_BACKEND=x11 tests/run.sh $T xfce4-appfinder` or
  xterm/xeyes; run.sh sets DISPLAY.
- Nested: run `build/owbox` with WAYLAND_DISPLAY set to the headless
  owbox's display, to test the Wayland backend.
- Logs: `--debug` (the test scripts use it) and `--debug-wlroots`. The last
  run's log is in `~/.cache/owbox/owbox.log`.
- Tests use `--no-autostart`, so the user's real XDG autostart applications
  don't start in test sessions.

Pitfalls seen before:
- Never `pkill -f` or `pgrep -f` with a pattern that's also in your own
  command line: it matches the shell running it. Kill by the PID
  (`$!`, `$T/pid`).
- Give `owbox-input` and `--msg` a `timeout`. If owbox hangs, they hang.
- Protect `rm` of variable paths: `rm -f "${T:?}"/*.png`.
- Coordinates in tests depend on the theme and the window's place. Read
  them from `--msg windows` instead of guessing (the frame is at x,y, and
  the titlebar's middle is about y+10).

## Where things are

See docs/ARCHITECTURE.md for the map. Quick pointers:
- input routing, grabs, focus delivery: `src/seat.c`
- Openbox's event handling (bindings, menus, focus follows mouse):
  `src/event.c`
- the asynchronous configure: `client_configure`, `client_committed`,
  `client_apply_configure` in `src/client.c`
- backends: `src/xdg.c`, `src/xwayland.c`, `src/prompt.c`
  (`ObClientImpl` in `src/client.h`)
- emulated X windows for decorations and menus: `src/fwin.c`
- `<owbox>` settings: `parse_owbox()` and `owbox_defaults()` in
  `src/config.c`
- new actions: `src/actions/`, registered in `src/actions/all.c`
- the scene graph's order: `server_init()` in `src/server.c`

## Packaging

`dist/arch/PKGBUILD` builds the committed HEAD (`makepkg -si` in that
directory). `meson install` installs the defaults to
`$sysconfdir/xdg/owbox`, the session to `$datadir/wayland-sessions`, and
the themes to `$datadir/owbox/themes`.
