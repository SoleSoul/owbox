# Workarounds for other projects' bugs

What owbox, or its documentation, does to get around a bug or a missing
feature in another project. Each entry says where the workaround is, what
the real fix would be and where to report it, so it can be reported and
removed when the other project is fixed.

When adding a workaround, add it here. When reporting one, record the
issue's link and when it was reported.

## In owbox's code

### GTK 3 dialogs without a parent

- **Where:** `handle_commit()` in `src/xdg.c` (`015235a`).
- **Problem:** some GTK 3 applications open dialogs without a parent
  (`gtk_window_set_transient_for()`), for example Thunar's Rename dialog
  (F2). Under X, GTK marks them as dialogs, and Openbox centers them over
  their application's windows (transient for the group). On Wayland, GTK 3
  sends no dialog type and gives such a dialog a parent only when its
  focused window is told it is focused, after the dialog was placed as a
  normal window.
- **Workaround:** when a client opens a new toplevel, owbox tells the
  client's focused toplevel again that it is activated. GTK then sends
  `set_parent` before the dialog is mapped, and it is placed like Openbox
  places it under X.
- **What others do:** Mutter, labwc and Sway place a window over its parent
  only if it has one when it is mapped, so they place Thunar's Rename
  dialog like a normal window. Sway's maintainer's advice in
  [thunar#1439](https://gitlab.xfce.org/xfce/thunar/-/issues/1439) is that
  the application should set the parent.
- **Real fix:** each application sets the dialog's parent. GTK also warns:
  "GtkDialog mapped without a transient parent. This is discouraged".
- **To report:** Thunar, at https://gitlab.xfce.org/xfce/thunar/-/issues
  (needs an Xfce GitLab account). Not reported yet. Draft:

  > **Rename dialog has no transient parent**
  >
  > The Rename dialog (F2) is created without a transient parent
  > (`xdg_toplevel.set_parent(nil)`, `WAYLAND_DEBUG=1 thunar` shows it), so
  > Wayland compositors can't place it over the Thunar window and place it
  > like a new window, usually away from the file being renamed. Under X it
  > has no `WM_TRANSIENT_FOR` either; window managers center it only
  > because of its dialog window type, which Wayland doesn't have. Setting
  > the parent window, as #1439 asks for other dialogs, fixes both.
  > Thunar 4.20.10, GTK 3.24.52.
- **Remove when:** not soon: other GTK 3 applications have the same
  omission, and the workaround costs one extra configure event per new
  window.

### Java applications under Xwayland

- **Where:** `session_environment()` in `src/session.c`, which sets
  `_JAVA_AWT_WM_NONREPARENTING=1` unless it is already set.
- **Problem:** Java's AWT draws blank windows under window managers that
  don't reparent, like Xwayland's.
- **Workaround:** the variable makes AWT work without reparenting. Sway and
  other Wayland compositors set it too.
- **Real fix:** in the JDK. Not for owbox to report.

### GTK 4 themes in a gresource lose their styles when reloaded

- **Where:** `SettingsApp.do_startup()` in `settings/owbox_settings/app.py`,
  which makes owbox Settings use GTK's own Adwaita theme.
- **Problem:** with GTK 4.24.1 (GLib 2.90.1), a GTK 4 theme whose CSS only
  imports its styles from a `gtk.gresource` (WhiteSur: `@import
  url("resource:///org/gnome/theme/gtk.css")`) works when an application
  starts, but when GTK loads it again, which it does whenever the light or
  dark preference changes, the import fails ("Failed to import: The
  resource at /org/gnome/theme/gtk-dark.css does not exist") and the
  application is drawn without styles (transparent windows). Changing the
  preference back doesn't help, restarting the application does. Plain
  GTK 4 applications are affected, GTK 3 and libadwaita ones aren't.
  Reproduced with a few lines of Python (a window that sets
  `gtk-theme-name` to WhiteSur-Dark and flips
  `gtk-application-prefer-dark-theme`), not with Adwaita. GLib registers
  and unregisters two loads of the same gresource correctly; GTK's
  `gtk_css_provider_load_named()` registers the new resource before
  loading the CSS and unregisters the old one in the reset, which looks
  right in the source, so the cause wasn't found.
- **Workaround:** owbox Settings, where the preference is changed, uses
  Adwaita, which follows the preference without reloading a gresource
  (most GTK 4 applications, made with libadwaita, look like it anyway).
- **Real fix:** in GTK (gtk_css_provider_load_named and the theme reload),
  https://gitlab.gnome.org/GNOME/gtk/-/issues. Not reported yet.
- **Remove when:** a GTK release reloads such themes correctly (check with
  the Python reproduction).

## In users' setups (documentation only)

### Chromium and Electron find no keyring

- **Where:** docs/CONFIG.md ("The session") and the ArchWiki draft
  (`dist/archwiki/owbox.mediawiki`, "Troubleshooting").
- **Problem:** Chromium and Electron choose a password store from the
  desktop's name and use none (plain text) for desktops they don't know,
  such as `XDG_CURRENT_DESKTOP=owbox`, even when a Secret Service
  (gnome-keyring) is running.
- **Workaround:** start them with `--password-store=gnome-libsecret`.
  Setting `XDG_CURRENT_DESKTOP` to GNOME would make other programs act as
  under GNOME, so owbox doesn't.
- **Real fix:** in Chromium, use the Secret Service when one is on the bus.
  Not reported by us; Chromium knows it chooses by the desktop's name.

### Claude Desktop's computer use (claude-desktop-extra)

The AUR package
[claude-desktop-extra](https://github.com/patrickjaja/claude-desktop-extra)
adds computer use to Claude Desktop on Linux. Two problems, both worked
around in the user's own setup (2026-10-21):

- **Not recognised as a wlroots compositor.** It uses its `wlroots-bridge`
  (screencopy, virtual keyboard and pointer, foreign-toplevel, all of which
  owbox offers) only when `SWAYSOCK`, `HYPRLAND_INSTANCE_SIGNATURE` or
  `NIRI_SOCKET` is set. Otherwise it types through XWayland, which reaches
  only X applications, and takes screenshots through the portal, which asks
  every time.
  - **Workaround:** a copy of `com.anthropic.Claude.desktop` in
    `~/.local/share/applications` with
    `Exec=env NIRI_SOCKET=owbox claude-desktop %U` (the app uses
    `NIRI_SOCKET` only for this check).
  - **Real fix:** detect wlroots compositors by their protocols; the
    bridge's own `wlroots-bridge doctor` already lists them.
- **Screenshots fail on NVIDIA.** wlroots offers screencopy in the
  renderer's preferred read format, which with NVIDIA's driver is
  `BGR888`; `wlroots-bridge` reports "unsupported screencopy pixel format
  Bgr888" and the app falls back to the portal. The same happens on Sway
  with NVIDIA.
  - **Workaround:** in `~/.config/owbox/environment`,
    `export COWORK_SCREENSHOT_CMD='grim -g "{X},{Y} {W}x{H}" {FILE}'`;
    grim reads any format.
  - **Real fix:** `wlroots-bridge` converts the 24-bit formats.
- **To report:** both at
  https://github.com/patrickjaja/claude-desktop-extra/issues. Not reported
  yet.

## Reporting

GitHub issues can be filed with `gh issue create -R <owner>/<repo>`, as
the user's account, after the user has seen and approved the text.
GitLab instances (Xfce, GNOME, freedesktop) need the user to file them.
Include the versions, the shortest reproduction (owbox's headless backend
and `WAYLAND_DEBUG=1` traces are good for that), and what other compositors
do.
