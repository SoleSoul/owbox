# owbox architecture

owbox is Openbox 3.6 (the git master of github.com/Mikachu/openbox) with
its X11 layer replaced by wlroots. The window management modules (client,
focus, stacking, placement, menus, actions, moveresize, keyboard and mouse
bindings, config) are Openbox's files, changed as little as possible. New
modules emulate what those modules expected from X. Knowing Openbox's
source is the best preparation for working on owbox. Where a function has
the same name as in Openbox, it does the same thing.

## The source

```
src/
  openbox.c         main(): startup/shutdown/reconfigure loop, options
  server.c          wlroots objects, the scene graph, the main loop glue
  seat.c            all input: keyboard, pointer, touch, focus, cursor, DnD
  event.c           Openbox's event.c for ObEvents: bindings, menus, focus
  output.c          monitors (wlr_output), output management, power
  layers.c          wlr-layer-shell (panels, wallpapers), session lock
  xdg.c             xdg-shell windows as Openbox clients
  xwayland.c        X windows as Openbox clients, override-redirect windows
  toplevel.c        foreign-toplevel, ext-workspace, xdg-activation, dialogs
  fwin.c            ObFWin: X windows emulated with scene trees
  buffer.c          cairo surfaces as wlr_buffers
  window.c          ObWindow/ObNode: what is at a point in the scene
  snap.c            edge snapping (an owbox addition)
  app_menu.c        applications-menu (an owbox addition)
  share_menu.c      the menu choosing what to share (owbox --choose-share)
  capture.c         screen capture formats for screenshots and sharing
  icons.c           desktop entries and icon themes (libsfdo)
  ipc.c             the control socket (owbox --msg)
  ping.c            pinging windows being closed
  session.c         environment, autostart, XDG autostart, the systemd target
  idle.c            monitors off when idle, locking (logind: Lock, suspend)
  render/           Openbox's obrender, drawing into cairo surfaces
  obt/              the parts of Openbox's libobt still needed (paths, xml,
                    keyboard helpers)
  actions/          Openbox's actions, plus snap.c
  everything else   Openbox's modules, ported (client.c, frame.c,
                    focus.c, stacking.c, place.c, menu*.c, moveresize.c,
                    keyboard.c, mouse.c, screen.c, prompt.c, popup.c,
                    focus_cycle*.c, resist.c, config.c, translate.c ...)
data/               default rc.xml, menu.xml, session files, themes
settings/           owbox Settings (Python, GTK 4): rcxml.py edits rc.xml
                    keeping what it doesn't know, keybinds.py the key
                    bindings, app.py the window
tools/              theme renderer, test input tool, helper scripts
tests/              headless test scripts and the smoke test
ref/                (gitignored) Openbox and wlroots sources for reference
```

## The main loop

Openbox runs on a glib main loop, with timeouts everywhere. owbox keeps it:
`server.c` adds the Wayland event loop's fd to the GMainLoop as a GSource,
whose prepare step flushes clients. wlroots timers and glib timeouts both
work, and Openbox's `g_timeout_add` code is unchanged.

Startup in `openbox.c`: load the configuration, `server_init()` (backend,
renderer, scene, protocols), `server_start()` (socket, backend, Xwayland's
DISPLAY), the IPC socket, the session environment, then Openbox's loop of
subsystem startups (theme, screen, focus, menus, ...). Autostart runs once,
after the first startup. Reconfigure, and restart too, runs that loop
again: shutdown(reconfig=TRUE), then startup(reconfig=TRUE).

## How X concepts became Wayland ones

| Openbox (X11) | owbox |
|---|---|
| XEvent | `ObEvent` (event.h), made by seat.c from wlroots input |
| X windows for frames, menus, popups | `ObFWin` (fwin.c), a scene tree with a background color or a painted buffer, children, a border, clipped to its ancestors like X |
| window_find(Window) | `ObNode` in the scene nodes' data, `window_at()` |
| X properties and client messages of a client | `ObClientImpl` (client.h), implemented by xdg.c, xwayland.c and prompt.c |
| XMoveResizeWindow + ConfigureNotify | `client_configure()` → `impl->configure()`, and the frame moves when the client commits its new size (see below) |
| XSetInputFocus, FocusIn/FocusOut | `seat_focus_client()` → `event_focus_changed()` |
| focus revert when the focused window unmaps | `client_hide()` schedules a `focus_fallback()` |
| passive grabs on the frame | seat.c: a press goes to Openbox's bindings first when the frame context has a binding for it, and the rest is "replayed" (forwarded) to the client |
| active grabs (grab.c) | flags: owbox keeps the keyboard or pointer events |
| XRestackWindows | `stacking.c` `sync_scene()`: the scene trees follow `stacking_list` |
| Xinerama heads | outputs (output.c), sorted left to right |
| _NET_WM_STRUT_PARTIAL | the same for X clients, and layer-shell exclusive zones become struts (layers.c) |
| _NET_ACTIVE_WINDOW, startup notification | xdg-activation (toplevel.c) |
| _NET_CLIENT_LIST, pagers | foreign-toplevel and ext-workspace (toplevel.c) |
| Motif hints for no decorations | xdg-decoration client-side mode, or no server-side decoration protocol (GTK 4) |
| _NET_WM_PING | xdg_wm_base.ping, or _NET_WM_PING via Xwayland, polled by ping.c |
| XKB, xmodmap | xkbcommon with `<owbox><keyboard>` |
| XWarpPointer | `seat_pointer_warp()` |
| X window groups | the same process (`client_same_group()`) |
| the root window | `server.root_rect` and clicks on nothing (`client == NULL`) |
| openbox --reconfigure (root message) | ipc.c |
| render: XImage/Pixmap/Xft | obrender into cairo image surfaces, text with pango |

## The scene graph

From the bottom up (server.c):

1. `root_rect`: the background color
2. `unmanaged_tree`: disabled, a parking place for surfaces that aren't
   shown
3. layer shell background, bottom
4. Openbox's stacking layers DESKTOP, BELOW, NORMAL, ABOVE (`stack_tree[]`)
5. layer shell top
6. stacking layer FULLSCREEN
7. layer shell overlay
8. `xwayland_tree`: X override-redirect windows (menus, tooltips)
9. stacking layer INTERNAL: Openbox's menus and popups
10. layer shell popups, drag icons, the lock screen

A managed client's surface tree is reparented into its frame's ObFWin, under
the frame's decorations, at the client's offset (the frame's size). The tree's
origin is the client's window geometry: wlroots' xdg scene tree moves the
surfaces by the geometry's offset, so client-side shadows hang outside it.

## Input

`seat.c` turns wlroots events into ObEvents for event.c and decides who gets
each event, the way the X server's grabs and event masks did:

- Keys: Openbox's bindings and menus see them first. A key that is used
  isn't sent to the client, and neither is its release; other keys are
  forwarded.
- A button press inside a frame element (titlebar, handle, buttons), or one
  with a binding in the Frame context, belongs to owbox: its release and the
  motion until then go to the same place. Presses in the client go to
  Openbox's bindings (Client context) and are then forwarded with
  `mouse_replay_pointer()`, unless a binding grabbed the pointer.
- Scrolling becomes X buttons 4 to 7, so Openbox's Up/Down/Left/Right mouse
  bindings work.
- The state given to bindings has its button masks removed, like Openbox's
  `event_hack_mods()`.

## Configures

An X window is resized right away and redraws later. A Wayland client draws
at the size it chooses. So `client_configure()` (client.c):

1. computes the geometry with Openbox's code (`client_try_configure`)
2. if the size changes and the window is shown, asks the client for it
   (`impl->configure`), remembers it as pending, and starts a 200 ms
   timeout; newer requests wait in `queued_area`
3. when the client commits a buffer (`client_committed`), applies the
   geometry (`client_apply_configure`, the frame half of Openbox's
   `client_configure`), anchored at the edges that weren't being moved,
   since the client may have chosen another size

X clients get their real root position with each configure, frame gravity
included (`client_root_position`), so their popups open in the right place.

## Client backends

`ObClientImpl` (client.h) has getters, which replace the property reading
in `client_get_all()` and the `client_update_*` functions, and requests:
configure, activate, close, change_state, ping, set_visible, restack. There
are three backends:
- **xdg.c**: xdg-shell toplevels, with xdg-decoration and KDE
  server-decoration for choosing decorations.
- **xwayland.c**: X windows. wlroots' xwm reads the properties, and this
  file maps them to ICCCM/EWMH semantics. ConfigureRequests are handled
  with Openbox's code from event.c.
- **prompt.c**: Openbox's own dialogs (ObPrompt) are managed clients whose
  "surface" is an ObFWin.

## Rendering

`render/` is obrender. `RrPaint` draws into a cairo ARGB32 image surface,
`buffer.c` wraps it as a wlr_buffer, and fwin.c puts it in a scene buffer.
Openbox's X-isms are emulated:
- an Xrm parser for themerc (render/xrm.c)
- X's color names (colornames.h, from X's rgb.txt)
- XBM masks
- `XDrawLine`-style bevels
- ParentRelative backgrounds, by copying the parent's pixels

Fonts use pango-cairo at 96 dpi with slight hinting and grayscale
antialiasing, like Xft's defaults.

owbox's scale (`<owbox><scale>`) is the instance's `RrInstanceSetScale()`,
set before rc.xml's fonts are opened (`config_read_scale()`, then the rest
of the file). Theme sizes go through `RrScaleInt()`, one pixel lines are
`RrLineWidth()` wide, masks are resampled into coverage (`mask.c`), and the
sizes hardcoded in Openbox's popups, menus and dialogs use `ob_scaled()`.

## Testing hooks

- `WLR_BACKENDS=headless` (tests/headless.sh) runs owbox without
  hardware. grim takes screenshots through screencopy.
- `owbox-input` (tools/) sends pointer and keyboard input with the virtual
  pointer and keyboard protocols.
- `owbox --msg windows` reports the state of the windows as JSON.
- `owbox-theme-render` renders a theme's decorations to a PNG.
