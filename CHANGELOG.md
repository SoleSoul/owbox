# Changelog

## Unreleased

- Fixed: owbox Settings was laid out right to left in a right-to-left
  language like Hebrew, with its English text. Until it is translated it
  keeps the left-to-right layout.

## 0.3.1 (2026-10-20)

- Fixed: a panel run as a systemd service (waybar.service) didn't start
  after logging out and in. Its display went away before owbox stopped the
  session's services, so it failed until systemd stopped restarting it.
  owbox now stops them while the display is still there, and clears failed
  services when the session starts.
- Fixed: menus and tooltips of bars (a tray icon's menu in waybar) never
  showed. Their popups get their parent from the layer surface after they
  are made, and owbox ignored them.
- Fixed: an application's menu stayed open when clicking outside of it,
  on the desktop, a frame or another window. The click now closes it, as
  in other compositors.
- Fixed: the example waybar configuration kept its bars from reserving
  space, so without `<autoHide>` they covered maximized windows' title
  bars. owbox already gives hidden panels' space to the windows. The smoke
  test now runs waybar and checks both.
- The descriptions say that owbox is based on Openbox's code, not that it
  is Openbox: it is an independent project, not affiliated with the Openbox
  developers.

## 0.3.0 (2026-10-19)

The first public release.

- **Tap Super** to bring up the panels and a launcher above the dimmed
  windows, fullscreen ones included, a moment to switch tasks that ends with
  Escape, a click elsewhere, picking a window in the taskbar, or tapping
  Super again. `<owbox><panels><autoHide>` keeps the panels hidden the rest
  of the time. This is the new `TogglePanels` action, `tap="yes"` key
  bindings (a modifier pressed and released alone), and `owbox-launcher`.
  `/usr/share/owbox/examples` has waybar (a thin top bar and a centered
  taskbar) and fuzzel configurations made for it.
- **owbox's scale** (`<owbox><scale>`, and Settings → Appearance → Size of
  owbox): window frames, menus, Alt-Tab and the other popups drawn larger
  than the theme, with their fonts, while applications keep their size.
  Themes stay sharp: bevels, borders and button bitmaps grow by whole
  numbers of pixels.
- owbox Settings: themes have their own page, finer sizes, readable names
  for media keys, and Key Bindings offers owbox's default bindings that an
  Openbox rc.xml (or a copy of an older owbox's) doesn't have, like Print
  Screen and the volume keys.
- **A session that works out of the box**:
  - The monitors turn off after 10 minutes without input (unless a video
    keeps them on), and the screen locks then, before suspending, and from
    the menu, with swaylock or another installed locker
    (`<owbox><idle>`, Settings → Displays).
  - The menu's new Leave submenu: Lock Screen, Suspend, Log Out, Restart,
    Shut Down.
  - The default rc.xml binds the volume and brightness keys to the new
    `owbox-volume` and `owbox-brightness`, which show the level in a
    notification, the media keys to playerctl, and Print Screen to the new
    `owbox-screenshot` (the screen, Alt for the focused window, Shift for
    an area).
  - The system autostart starts an installed polkit agent, `owbox-session`
    points `SSH_AUTH_SOCK` at the SSH agent systemd started, and owbox
    starts `owbox-session.target` (graphical-session.target), so the
    session's services stop at logout.
- `owbox --msg windows` reports each frame's size around its window.
- Fixed: owbox crashed when a monitor's scale changed after a reconfigure
  while the pointer was over an application using its own cursor shape.
- Fixed: a `~` at the end of a command wasn't expanded (an Openbox bug), so
  the default Super+E (`xdg-open ~`) did nothing.

## 0.2.0 (2026-10-19)

- owbox Settings: appearance (themes with previews), windows, displays,
  desktops, keyboard, mouse and touchpad, key bindings and autostart, with
  help moving from Openbox.
- Screen sharing of monitors and windows, with owbox's own menu to choose
  what to share, and 32 bit capture formats on GPUs that prefer 24 bit
  (NVIDIA).
- Fixes from daily use: submenus, menus closed with the keyboard, client
  side decorated windows' position, rolling up and un/decorating them, key
  bindings in other keyboard layouts, services left by an earlier session.

## 0.1.0 (2026-10-19)

Openbox's window management on wlroots: frames, themes, focus, stacking,
placement, menus, Alt-Tab, desktops, move and resize, all actions,
per-application settings. xdg-shell, Xwayland, layer shell, session lock,
and the protocols panels, launchers and screenshot tools use. The `<owbox>`
settings (keyboard, input devices, outputs, themes, decorations, snapping),
the applications menu, the control socket, reload on change.
