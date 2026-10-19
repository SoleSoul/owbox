# Changelog

## 0.3.0 (2026-10-19)

The first public release.

- **owbox's scale** (`<owbox><scale>`, and Settings → Appearance → Size of
  owbox): window frames, menus, Alt-Tab and the other popups drawn larger
  than the theme, with their fonts, while applications keep their size.
  Themes stay sharp: bevels, borders and button bitmaps grow by whole
  numbers of pixels.
- owbox Settings: themes have their own page, finer sizes, readable names
  for media keys, and Key Bindings offers owbox's default bindings that an
  Openbox rc.xml (or a copy of an older owbox's) doesn't have, like Print
  Screen and the volume keys.
- **A session that works out of the box**: the default rc.xml binds the
  volume, media and brightness keys, and Print Screen to the new
  `owbox-screenshot` (the screen, Alt for the focused window, Shift for an
  area). The system autostart starts an installed polkit agent, and
  `owbox-session` points `SSH_AUTH_SOCK` at the SSH agent systemd started.
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
