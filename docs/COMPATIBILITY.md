# Compatibility with Openbox

owbox runs Openbox 3.6's window management code, so behavior that isn't
listed here should be the same as in Openbox. A difference that isn't
listed is a bug.

## Works the same

- rc.xml: all sections. That covers resistance, focus (including
  focus-follows-mouse, delay, underMouse), placement (smart, under mouse,
  center, monitor policy), theme (name, titleLayout, fonts, keepBorder,
  animateIconify, window list icon size), desktops (number, names, popup
  time), resize (drawContents, popups), margins, menu settings, key bindings
  (chains, keycodes, chroots), mouse bindings (all contexts and events,
  double click time, drag threshold, screen edge warp), and per-application
  settings.
- All of Openbox's actions, with the exceptions below.
- menu.xml: menus, separators, icons, pipe menus, `client-list-menu`,
  `client-list-combined-menu`, `client-menu`.
- Themes: the full themerc format (gradients, borders, bevels, masks from
  XBM files, PNG and SVG buttons, shadows on text, ParentRelative), so
  everything in `/usr/share/themes/*/openbox-3` works.
- Alt-Tab (window list dialog and focus cycle indicator), directional focus,
  the desktop switching popup, move and resize popups, edge resistance,
  screen-edge desktop switching while moving, show desktop, and the "not
  responding" kill dialog.
- Xinerama-style multi-monitor behavior, with each output as a monitor.
- `openbox --reconfigure` and `--exit` become `owbox --reconfigure` and
  `--exit`.

## Different, because Wayland works differently

- **Restart** (the action and `--restart`) reconfigures. A Wayland
  compositor can't restart without closing every window.
- **Window positions are owbox's.** Wayland applications can't place
  themselves, so owbox places them with Openbox's placement code, and
  `<position>` in per-application settings works for them. X applications
  can still ask for positions, as in Openbox.
- **Window groups** don't exist in Wayland. Where Openbox uses the X window
  group (group transients, "the same application" in placement and focus),
  owbox uses windows of the same process.
- **Applications drawing their own decorations.** GTK 4/libadwaita
  applications decorate themselves unless told otherwise. With the default
  `<owbox><decorations>auto`, they're treated like X windows with Motif
  hints asking for no decorations. Their own title bar buttons and window
  menu (right click on their title) work, and the window menu they open is
  Openbox's client menu.
- **Wayland resizes are asynchronous.** A Wayland application chooses its
  size, and owbox shows the frame at the new size once the application has
  drawn it. Terminals snap to whole character cells, as with X size hints.
- **The autostart script** is read from the owbox config directories only,
  not `~/.config/openbox/autostart`, because Openbox autostart scripts
  usually start X programs (xcompmgr, xset, tint2, nitrogen) that have
  Wayland replacements.
- **Root window commands.** There is no X root window: set the background
  with `<owbox><background>` or a wallpaper program (swaybg). Keyboard
  layouts and input device settings go in `<owbox>` instead of
  setxkbmap/xinput, and monitors in `<owbox><outputs>` or wlr-randr instead
  of xrandr.
- **ObConf** is replaced by owbox Settings, which also covers what Openbox
  left to X tools (keyboard layouts, mouse and touchpad, monitors) and key
  bindings. obconf-qt still works through Xwayland, if pointed at the file
  owbox uses.
- **Scripting** with xdotool and wmctrl doesn't work for Wayland windows:
  use `owbox --msg` (actions, window and desktop lists). Taskbars and pagers
  use the foreign-toplevel and ext-workspace protocols.
- **Startup notification** uses xdg-activation tokens:
  `<startupnotify><enabled>yes` in Execute gives the launched application a
  token, so it is focused (with focus stealing prevention) when it opens.

## Not supported (yet)

- **The dock.** WindowMaker dockapps are an X feature. The `<dock>` settings
  and dock actions are accepted and ignored. Panels use wlr-layer-shell,
  which reserves screen space the same way docks' struts did.
- **X pagers and taskbars** (tint2, X builds of xfce4-panel) don't see the
  windows, and X windows can't move themselves to other desktops
  (`_NET_WM_DESKTOP`) because wlroots' X window manager doesn't expose it.
  Use a Wayland panel.
- **Session management** (XSMP) is not supported: applications don't get
  their windows restored on login.
- **HiDPI decorations** are drawn at scale 1 and scaled up on outputs with a
  scale above 1, so they look soft. Windows' contents are sharp.
- **`--replace`**: a Wayland compositor can't take over a running session.

## Extensions

These have no Openbox equivalent and are described in
[CONFIG.md](CONFIG.md): the `<owbox>` section, the `SnapToEdge` and `Unsnap`
actions, `applications-menu`, the control socket, and reloading rc.xml when
it changes.
