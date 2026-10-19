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

## Next

- **Test on real hardware**: the DRM backend on a TTY (NVIDIA and others),
  multiple monitors, hotplug, VT switching, suspend and resume.
- **Exercise the layer shell and session lock** with real clients (waybar,
  swaybg, fuzzel, swaylock) and add them to the smoke test.
- **HiDPI decorations**: render frames, menus and popups at the output's
  scale (obrender painting at a scale factor), so they're sharp on scale 2
  and fractional scales.
- **Touchpad gestures as bindings**, for example
  `<mousebind button="Swipe3Left" action="Gesture">` in rc.xml.
- **xdg-toplevel-icon**, so Wayland clients can set their window icon.
- **Text input / input methods** (text-input-v3, input-method-v2) for
  CJK input.
- **Tablets** (tablet-v2).
- **Optional eye candy**, off by default: rounded corners, shadows,
  window open/close and iconify animations, opacity rules per application.

## Later, maybe

- `_NET_WM_DESKTOP` and X pagers, which need support in wlroots' xwm
- a dock for X dockapps under Xwayland
- per-monitor desktops, as an option
- live window thumbnails in the Alt-Tab dialog
