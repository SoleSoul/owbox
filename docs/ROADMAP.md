# Roadmap

## 0.1.0 (done)

- Openbox's window management on wlroots: frames, themes, focus,
  stacking, placement, menus, Alt-Tab, desktops, move/resize, all actions,
  per-application settings, the kill dialog
- xdg-shell, Xwayland (managed and override-redirect windows), layer shell,
  session lock, and the protocols panels, launchers, screenshot and screen
  sharing tools use
- `<owbox>` settings: keyboard, libinput, outputs, cursor and icon themes,
  decorations, background, snapping, desktop layout, Xwayland
- snapping, `applications-menu`, the control socket, reload on change
- default config, session files, man page, Arch package, smoke test

## 0.2.0 (done)

- runs as a daily session on real hardware (NVIDIA, DRM backend, VT
  switching), with waybar, swaybg and fuzzel
- owbox Settings: appearance, windows, displays, desktops, keyboard,
  mouse and touchpad, key bindings, autostart (with Openbox migration)
- screen sharing of monitors and windows, with owbox's own chooser menu;
  32 bit capture formats on GPUs that prefer 24 bit (NVIDIA)
- fixes from daily use: submenus, menus closed with the keyboard, client
  side decorated windows' position, shading, un/decorating, bindings in
  other keyboard layouts, leftover services of an earlier session

## 0.3.0 (done)

- tapping Super shows the panels and a launcher over the dimmed windows
  (`tap="yes"` key bindings, the `TogglePanels` action, `<owbox><panels>`
  to keep them hidden otherwise), with example waybar and fuzzel
  configurations
- `<owbox><scale>`: Openbox themes drawn larger and sharp (frames, menus,
  popups and their fonts), without making applications larger, in owbox
  Settings' Appearance page
- a session that works out of the box: monitors off when idle and locking
  (`<owbox><idle>`, with swaylock), Lock, Suspend, Restart and Shut Down in
  the menu, volume and brightness keys that show the level, screenshots
  with Print Screen (`owbox-screenshot`), a polkit agent, the SSH agent,
  `graphical-session.target`
- fixes: a crash when changing a monitor's scale after a reconfigure, `~`
  at the end of a command (the default Super+E)
- first public release, on GitHub and the AUR

## 0.3.1 (done)

- fixes: bars' menus and tooltips, closing menus by clicking elsewhere,
  panels run as services after logging in again, the example waybar's
  bars covering maximized windows

## 0.3.2 (done)

- fixes: the screen sharing menu offers only what the application asked
  for, GTK 3 dialogs without a parent are centered over their application,
  the pointer starts in the middle, virtual keyboards' first keys
- programs' messages go to the journal when owbox runs on a console

## Next

- **More of owbox Settings**: mouse bindings, the menus, per-application
  rules, margins, monitor arrangement by dragging.
- **Test more hardware**: multiple monitors, hotplug, suspend and resume,
  other GPUs.
- **Exercise the session lock** (swaylock, swayidle) and add the layer
  shell clients to the smoke test.
- **HiDPI decorations**: render frames, menus and popups at the output's
  scale too, with the same code as owbox's scale (obrender drawing at a
  factor), so they're sharp on outputs at scale 2 and fractional scales.
- **Touchpad gestures as bindings**, for example
  `<mousebind button="Swipe3Left" action="Gesture">` in rc.xml.
- **xdg-toplevel-icon**, so Wayland clients can set their window icon.
- **Text input / input methods** (text-input-v3, input-method-v2) for
  CJK input.
- **Tablets** (tablet-v2).
- **Optional eye candy**, off by default: rounded corners, shadows,
  window open/close and iconify animations, opacity rules per application.

- **A guide to a complete desktop** (an Arch wiki page): for each kind of
  companion program (bar, launcher, notifications, locker, file manager,
  ...) the one that fits owbox best and how to set it up.

## Later, maybe

- `_NET_WM_DESKTOP` and X pagers, which need support in wlroots' xwm
- a dock for X dockapps under Xwayland
- per-monitor desktops, as an option
- live window thumbnails in the Alt-Tab dialog
