# Standards

The specifications owbox follows, and how far. Gaps that are planned are in
[ROADMAP.md](ROADMAP.md), the ones left out on purpose are explained in
[DECISIONS.md](DECISIONS.md). The Openbox formats (rc.xml, menu.xml,
themes) are in [COMPATIBILITY.md](COMPATIBILITY.md).

## Wayland protocols

owbox offers these, with wlroots 0.20's implementations, each at the
newest version wlroots has:

| Area | Protocols |
|---|---|
| Surfaces and buffers | wl_compositor, wl_subcompositor, wl_shm, linux-dmabuf (and wl_drm), linux-drm-syncobj (when the GPU's driver supports explicit sync), single-pixel-buffer, viewporter, fractional-scale, alpha-modifier, presentation-time, content-type, tearing-control, wl_fixes |
| Windows | xdg-shell, xdg-decoration, KDE server-decoration, xdg-activation, xdg-dialog, xdg-foreign (v1 and v2), xdg-toplevel-icon, xdg-system-bell |
| Desktop components | wlr-layer-shell, ext-session-lock, ext-idle-notify, idle-inhibit, ext-foreign-toplevel-list, wlr-foreign-toplevel-management, ext-workspace, security-context |
| Input | wl_seat, wl_data_device, primary-selection, wlr-data-control, ext-data-control, cursor-shape, pointer-constraints, relative-pointer, pointer-gestures, keyboard-shortcuts-inhibit, virtual-keyboard, wlr-virtual-pointer, tablet-v2, text-input-v3, input-method-v2 |
| Outputs and capture | xdg-output, wlr-output-management, wlr-output-power-management, wlr-gamma-control, wlr-screencopy, wlr-export-dmabuf, ext-image-copy-capture (monitors and windows) |

Left out: color-management and color-representation, drm-lease,
transient-seat and xdg-toplevel-tag.

## X applications (ICCCM and EWMH)

X applications run in Xwayland, whose window manager is wlroots' xwm.
owbox handles what xwm passes on:

- ICCCM: WM_CLASS, WM_WINDOW_ROLE, WM_TRANSIENT_FOR, WM_NORMAL_HINTS,
  WM_HINTS (input, urgency, initial state, window group), WM_TAKE_FOCUS,
  WM_DELETE_WINDOW.
- EWMH: _NET_WM_WINDOW_TYPE, _NET_WM_STATE (maximized, fullscreen, shaded,
  above, below, hidden, skip taskbar and pager, demands attention),
  _NET_ACTIVE_WINDOW, _NET_WM_STRUT_PARTIAL, _NET_WORKAREA, _NET_WM_ICON,
  _NET_WM_PING, _NET_STARTUP_ID, and the Motif hints for decorations.

Missing: the desktops (_NET_WM_DESKTOP, _NET_NUMBER_OF_DESKTOPS), which X
pagers and taskbars need and xwm doesn't pass on. _NET_WM_STATE_STICKY is
ignored, as in Openbox.

## freedesktop.org

| Specification | Status |
|---|---|
| XDG Base Directory | configuration, data and cache in the `XDG_*` directories |
| Desktop Entry | the applications menu and XDG autostart (with libsfdo): Hidden, NoDisplay, OnlyShowIn, NotShowIn, TryExec, Exec field codes, Path, Terminal, StartupNotify, StartupWMClass, Icon |
| Desktop Application Autostart | `~/.config/autostart` and the system's, for `XDG_CURRENT_DESKTOP=owbox` |
| Icon Theme | window, menu and Alt-Tab icons, from the GTK icon theme |
| Cursor themes | `XCURSOR_THEME` and `XCURSOR_SIZE` for applications, cursor-shape for the rest |
| Startup notification | an xdg-activation token in `XDG_ACTIVATION_TOKEN` and `DESKTOP_STARTUP_ID`; X applications that return it in `_NET_STARTUP_ID` open on their desktop |
| Desktop Menu | not followed: the applications menu goes by the desktop entries' main category, without `applications.menu` files |
| Idle inhibition | `org.freedesktop.ScreenSaver` (Inhibit, UnInhibit, SimulateUserActivity, GetActive, Lock, ActiveChanged) on the session bus |
| xdg-desktop-portal | `owbox-portals.conf`: xdg-desktop-portal-wlr for Screenshot and ScreenCast, xdg-desktop-portal-gtk for the rest (its Inhibit uses owbox's ScreenSaver) |
| xdg-terminal-exec | `owbox-terminal` uses it, after `$TERMINAL` |

## systemd and logind

- The session: `owbox-session.target`, bound to `graphical-session.target`,
  with the environment (`WAYLAND_DISPLAY`, `DISPLAY`,
  `XDG_CURRENT_DESKTOP`, `XDG_SESSION_TYPE`, `XDG_SESSION_DESKTOP`)
  imported into systemd and D-Bus activation.
- logind: Lock locks the screen, PrepareForSleep locks it before
  suspending (with a delay inhibitor), idle block inhibitors
  (`systemd-inhibit --what=idle`) keep the monitors on, and owbox sets the
  session's IdleHint and LockedHint. Unlock is ignored: with
  ext-session-lock only the screen locker unlocks.

## Keyboards

xkbcommon with xkeyboard-config's layouts, variants and options, set in
`<owbox><keyboard>` or by the `XKB_DEFAULT_*` variables.
