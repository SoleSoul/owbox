# Roadmap

What is done is in [CHANGELOG.md](../CHANGELOG.md).

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
- **Publish [GUIDE.md](GUIDE.md) on the ArchWiki** (see AGENTS.md).

## Later, maybe

- `_NET_WM_DESKTOP` and X pagers, which need support in wlroots' xwm
- a dock for X dockapps under Xwayland
- per-monitor desktops, as an option
- live window thumbnails in the Alt-Tab dialog
- screenshots and screen sharing of a powered-off monitor (wlroots refuses
  a disabled output; it needs rendering the capture offscreen)
