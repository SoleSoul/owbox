# Working on owbox

owbox is a Wayland compositor on wlroots built from Openbox's code (an
independent project, not Openbox). Read README.md for what it
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
- Say that owbox is *based on* Openbox, never that it is Openbox (in
  descriptions, release notes, packages, the guide).
- A workaround for another project's bug goes in docs/WORKAROUNDS.md, with
  where it is, the real fix and where to report it. File reports only
  after the user has approved the text.

## Git

- Work on main. Commit finished, tested work without asking. Push, tag
  and release only when the user asks.
- The subject says what changed, as the user sees it ("Unsnap restores
  maximized windows"), with an area prefix for what isn't the compositor
  (`Settings:`, `Docs:`, `CI:`, `dist/arch:`). Release commits are
  `owbox X.Y.Z`.
- End commit messages with a `Co-Authored-By:` line naming the agent's
  model, like `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Handoff between sessions

`HANDOFF.md` at the top (local, excluded from git in `.git/info/exclude`)
says where the last session left off, whichever agent ran it. It holds
only transient work left to be done that doesn't belong in the tracked
docs, and is empty otherwise. Read it before starting. Before finishing
(a task done, or work left uncommitted), rewrite it with what is left:

- the git state: unpushed commits, and each uncommitted change with what
  it is, which request it came from and whether the user approved it;
- open threads: unfinished work, questions waiting for the user, things
  left untested.

Keep it the current state, not a log: drop what is done, and leave the
file empty when nothing is left. Durable context goes in the tracked
docs, not here. If the working tree has changes it doesn't explain, find
out whose they are (ask the user, or search your earlier sessions)
before committing or reverting them.

## Documentation

Development must not depend on any one session or agent: what a later
session needs goes in the repo, not in an agent's own memory or notes.
Update the documents when a feature or version is done, not mid-work.

Where things go:

| What | Where |
|---|---|
| what owbox is, installing, running | README.md |
| using it with companion programs (written as an ArchWiki article) | docs/GUIDE.md |
| `<owbox>` settings, owbox's actions, the control socket | docs/CONFIG.md, the comments in data/rc.xml, docs/owbox.1 |
| differences from Openbox | docs/COMPATIBILITY.md |
| standards followed, and how far | docs/STANDARDS.md |
| how the code works, a map of it | docs/ARCHITECTURE.md |
| why it is so, with the alternatives | docs/DECISIONS.md |
| what's next | docs/ROADMAP.md |
| what changed, for users | CHANGELOG.md, under `## Unreleased` until a release |
| other projects' bugs | docs/WORKAROUNDS.md |
| how to work on it (for agents and people) | AGENTS.md |
| where the last session stopped | HANDOFF.md |

Keep each document short and current:
- Say each thing once, in the place above, and link to it elsewhere.
- Write the current state, not history: git and the CHANGELOG keep
  history. Remove what is done from ROADMAP.md, and fixed workarounds from
  WORKAROUNDS.md.
- When changing a document, remove what has become wrong or obsolete, and
  shorten what can be said in fewer words.
- Plain, short sentences, saying what happens, as the existing documents
  do.
- AGENTS.md is for what an agent can't find out from the code: rules,
  commands, pitfalls. Add a pitfall when one cost real time.
- Keep docs/GUIDE.md's recommendations in step with the defaults. To put
  it on the ArchWiki, convert it to wiki markup (`{{Pkg}}`, `{{AUR}}`,
  `{{ic}}`, `[[links]]`); the last wiki-markup draft and how to publish it
  are in `git show cf7e61f:dist/archwiki/`.

## Building

    meson setup build          # once
    ninja -C build

This builds `build/owbox`, `build/owbox-input` (the test input tool) and
`build/owbox-theme-render`. compile_commands.json is symlinked at the top
for clangd. Dependencies are listed in README.md.

The reference sources in `ref/` are gitignored. To recreate them:

    git clone https://github.com/Mikachu/openbox.git ref/openbox   # owbox is ported from ebfa727
    git clone -b 0.20.2 https://gitlab.freedesktop.org/wlroots/wlroots.git ref/wlroots
    git clone -b v0.8.4 https://github.com/emersion/xdg-desktop-portal-wlr.git ref/xdg-desktop-portal-wlr

## Testing

Run the smoke test after changes, and `tests/settings_test.py` after
changing owbox Settings or the config parser:

    tests/smoke.py            # --keep keeps the log and screenshots

CI also checks the Python code with `pyflakes`, and runs the smoke test
built with AddressSanitizer and UBSan, failing on any report. To run that
here:

    meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false \
        -Dc_args=-fno-sanitize=shift-base
    ninja -C build-asan && OWBOX_BUILD=build-asan tests/smoke.py

`tools/ci-local.sh` runs all of CI's steps in a clean Arch container.

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
- Screen sharing: `tools/screencast-test.py 1 5` runs a portal session
  (1 monitors, 2 windows) and counts the frames with build/owbox-pwcount.
  It needs WirePlumber running (nothing links the streams without it).  To
  test a headless owbox, give it portals on a bus of their own, so the
  session's aren't touched:
  `XDG_CURRENT_DESKTOP=owbox tests/run.sh $T dbus-daemon --session --fork
  --print-address=1 --print-pid=1 > $T/bus`, then run the test, a browser
  (`zen-browser --no-remote --profile $T/zen` with a page calling
  `getDisplayMedia()`) or `/usr/lib/xdg-desktop-portal-wlr -l DEBUG` with
  `DBUS_SESSION_BUS_ADDRESS=$(head -1 $T/bus)`. Afterwards kill the
  processes on that bus too (the portals it started), found by the bus
  address in `/proc/PID/environ`. Use `WLR_RENDERER=gles2` to test the
  GPU's formats. A window alone: `grim -T <identifier>` (the identifier is
  in `--msg windows`).
- Logs: `--debug` (the test scripts use it) and `--debug-wlroots`. The
  user's session logs to `~/.cache/owbox/owbox.log`; headless.sh gives
  test runs their own cache, so they don't replace it.
- Tests use `--no-autostart`, so the user's real XDG autostart applications
  don't start in test sessions.
- The headless backend (pixman) isn't the user's hardware: their daily
  session is owbox on an NVIDIA GPU (proprietary driver, DRM backend),
  where capture formats differ (BGR888). Graphics and capture changes need
  a test there too, or the user's confirmation. Right-to-left problems
  showed up with a Hebrew locale.

Pitfalls seen before:
- Never `pkill -f`, `pgrep -f` or `ps | grep` + kill with a pattern that
  is anywhere in your own command line, including in a variable's value or
  a path: it matches the shell running it. Kill by the PID (`$!`,
  `$T/pid`), and list candidates before killing anything. Never let a PID
  be 0 or empty: `kill 0` signals your own process group.
- When testing inside the user's owbox session, OWBOX_SOCK points at their
  owbox: tests/run.sh unsets it, other commands must too.  Only a DRM
  session updates the D-Bus/systemd environment, keep it that way.
- Give `owbox-input` and `--msg` a `timeout`. If owbox hangs, they hang.
- Clients started with `tests/run.sh $T ... &` can outlive the headless
  owbox (a stray waybar stays on the user's session bus as a tray host).
  Keep their PIDs (`$!`) and kill them after `tests/stop.sh`.
- Protect `rm` of variable paths: `rm -f "${T:?}"/*.png`.
- Coordinates in tests depend on the theme and the window's place. Read
  them from `--msg windows` instead of guessing (the frame is at x,y, and
  the titlebar's middle is about y+10).

## owbox Settings

`settings/` is a Python and GTK 4 application (`settings/owbox-settings`
runs it from the source tree).  `rcxml.py` edits rc.xml in place, keeping
comments and everything it doesn't know; `tests/settings_test.py` checks
that, and that owbox loads the result.  When trying the app, give owbox
and the app a scratch config (`XDG_CONFIG_HOME=$T/cfg`, with
`$T/cfg/owbox/rc.xml`): the app saves changes to the system's file in the
user's `~/.config/owbox`.

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
- idle, locking and logind: `src/idle.c` (logind and
  org.freedesktop.ScreenSaver only on the DRM backend, so tests don't lock
  or keep awake the user's session; `OWBOX_TEST_BUSES=1` uses them anyway,
  for a test with its own session and system buses, as `test_idle_dbus`
  in the smoke test does with a mock logind)
- the panels mode (Super tap): `src/panels.c`, the panels themselves in
  `src/layers.c` (`layers_is_panel`), tap bindings in `src/keyboard.c`

## Packaging

`dist/arch/PKGBUILD` builds the committed HEAD (`makepkg -si` in that
directory). makepkg changes `pkgver=` in it on each build: never commit
that, restore it with `git checkout dist/arch/PKGBUILD`. `meson install`
installs the defaults to
`$sysconfdir/xdg/owbox`, the session to `$datadir/wayland-sessions`, and
the themes to `$datadir/owbox/themes`.

## Releasing

owbox is on GitHub (SoleSoul/owbox, whose CI runs the smoke and settings
tests on Arch) and the AUR (`owbox` from the release tarball, `owbox-git`
from the latest commit). When the user asks for a new version, do the
whole release:

1. Choose the version: X.Y+1.0 with new features, X.Y.Z+1 with only
   fixes.
2. Run `tools/ci-local.sh` (CI's steps in a clean Arch container, which
   also catches missing dependencies).
3. Set the version in `meson.build`, the `.TH` line of docs/owbox.1 and
   README.md's status line; rename CHANGELOG.md's `## Unreleased` to
   `## X.Y.Z (date)`, and remove what it ships from ROADMAP.md.
4. Commit `owbox X.Y.Z`, tag it (`git tag -a vX.Y.Z -m "owbox X.Y.Z"`),
   push main and the tag, and check that CI passes.
5. `gh release create vX.Y.Z --title "owbox X.Y.Z"`, with the CHANGELOG
   section as the notes.
6. Update the AUR `owbox` package. Its clone and steps are on the user's
   machine in `~/ai/AUR` (see its AGENTS.md); pushing needs the user's SSH
   key unlocked. `owbox-git` needs nothing.

A pushed tag never moves: late fixes go in the next version.
