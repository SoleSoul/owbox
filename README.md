# owbox — a Wayland compositor based on Openbox

owbox is a stacking Wayland compositor based on
[Openbox](http://openbox.org): built from Openbox's window management code,
for people who like Openbox and want to use Wayland.

> **owbox is not Openbox.** It is an independent project that reuses
> Openbox's source code under the GPL. It isn't made, endorsed or supported
> by the Openbox developers: please report problems with owbox here, not to
> Openbox.

It aims for Openbox's goals (standards compliant, complete, light, sane
defaults), and to make moving from Openbox easy:

- **Openbox's configuration.** owbox reads `rc.xml` and `menu.xml` from
  `~/.config/owbox/`, or from `~/.config/openbox/` if there is no owbox one.
  Key and mouse bindings, actions, per-application rules, margins, desktops,
  placement and focus settings are meant to work as in Openbox.
- **Openbox's themes.** Openbox themes (`openbox-3/themerc`) are drawn by
  Openbox's own theme engine, ported to cairo and pango.
- **Openbox's behavior.** Window management, focus, stacking, placement,
  menus, Alt-Tab, the desktop switcher, move and resize with resistance are
  Openbox's code, adapted to run on
  [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) in place of
  Xlib. Where owbox differs, [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)
  says so.
- **X applications** run through Xwayland and are managed like in Openbox,
  ICCCM and EWMH hints included.

Openbox is a window manager that its users build a desktop around, finding
and setting up the other programs a session needs. owbox aims to be more
complete out of the box: an applications menu, a settings application,
screen locking, monitors that turn off when idle, and keys for the volume,
brightness and screenshots work from the first login (using the usual
programs for them, swaylock, grim and the like, when they are installed). It
adds what Openbox left to the X server (keyboard layouts, input devices,
monitors), and what most Openbox users add on top of it:

- snapping windows to the screen's halves and quarters by dragging, or with
  Super+arrows
- a scale for owbox itself: Openbox themes drawn larger and sharp on high
  resolution screens, without making applications larger
- tap Super to bring up the panels and a launcher over the dimmed desktop,
  a moment to switch tasks that ends with Escape or picking a window;
  panels can stay hidden the rest of the time (example waybar and fuzzel
  configurations included)
- a built-in applications menu generated from the installed applications
- **owbox Settings**, one application for all of it: themes (with
  previews), the look of applications (GTK theme, icons, cursor, fonts,
  light or dark, and title bars drawn by applications that match owbox's
  buttons and clicks), windows, displays, desktops, keyboard layouts, mouse and
  touchpad, key bindings (press the keys, pick the action) and autostart.
  It edits the same rc.xml, keeps what it doesn't show, and helps moving
  from Openbox
- screen sharing (of a monitor or a window) with an Openbox style menu to
  choose what to share
- what a desktop session needs out of the box: monitors that turn off when
  idle, locking (when idle, before suspending, from the menu), Lock,
  Suspend, Restart and Shut Down in the menu, volume and brightness keys
  that show the level, screenshots (Print Screen), a polkit agent, the SSH
  agent, and the systemd graphical session
- a control socket for scripting (`owbox --msg`)
- automatic reload when `rc.xml` changes, so any editor's changes apply
  immediately

See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for what differs from
Openbox, and [docs/ROADMAP.md](docs/ROADMAP.md) for what is planned.

Status: version 0.6.0, early but usable. It is used as a daily session on
real hardware (an NVIDIA GPU on the DRM backend), with Wayland and X
applications, waybar, swaybg, fuzzel and screen sharing in a browser.
Multiple monitors and hotplugging haven't been tried yet. Bug reports are
welcome, especially where owbox behaves differently from Openbox.

## Installing

On Arch Linux, from the AUR: `yay -S owbox` (or any AUR helper), or
`owbox-git` for the latest commit.

## Building

Dependencies (Arch package names): `wlroots0.20 libinput libxkbcommon pixman
glib2 libxml2 pango cairo librsvg libsfdo xcb-util-wm wayland
wayland-protocols meson ninja`. Install `xorg-xwayland` to run X
applications, `python-gobject` and `gtk4` for owbox Settings, and
`xdg-desktop-portal-wlr` with PipeWire and WirePlumber for screen sharing.

On Arch, `dist/arch` has a package of the working copy's last commit:

    cd dist/arch && makepkg -si

Or with meson directly:

    meson setup build --prefix=/usr --sysconfdir=/etc
    ninja -C build
    sudo ninja -C build install

(The default prefix `/usr/local` works too, but some display managers only
look for sessions in `/usr/share/wayland-sessions`.)

## Running

- **From a display manager**, choose the *owbox* session.
- **From a text console**, run `owbox-session`. It reads
  `~/.config/owbox/environment` first, like `openbox-session` does.
- **Nested**, inside another Wayland or X session, run `owbox`. It opens a
  window to try it out in.

Programs to start with the session go in `~/.config/owbox/autostart`, a shell
script, as with Openbox. Applications in the XDG autostart directories
(`~/.config/autostart`) are started too.

A complete desktop needs a few other programs: a panel (waybar), a launcher
(fuzzel), a wallpaper (swaybg), a screen locker (swaylock), notifications,
screenshots (grim, slurp)... [docs/GUIDE.md](docs/GUIDE.md) recommends one
for each, and owbox includes example configurations for waybar and fuzzel
in `/usr/share/owbox/examples`.

## Configuration

Run **owbox Settings** (`owbox-settings`, also in owbox's menu). Changes
apply as they are made. If you come from Openbox, it uses your Openbox
rc.xml and offers to keep a separate copy for owbox, and its Autostart page
shows what your Openbox autostart ran and what replaces the X programs.

Or copy the defaults and edit them:

    mkdir -p ~/.config/owbox
    cp /etc/xdg/owbox/{rc.xml,menu.xml,autostart,environment} ~/.config/owbox/

Or keep using your Openbox files: with no `~/.config/owbox/rc.xml`, owbox
reads `~/.config/openbox/rc.xml`. Openbox's documentation applies:
<http://openbox.org/wiki/Help:Configuration>.

owbox's own settings go in an `<owbox>` section of `rc.xml`, which Openbox
ignores, so one file serves both. They cover the keyboard layout, touchpad
and mouse settings, monitors, owbox's scale, cursor and icon themes,
decorations, snapping and Xwayland. The installed `rc.xml` documents each of them, and
[docs/CONFIG.md](docs/CONFIG.md) is the reference.

`owbox --reconfigure` reloads the configuration (saving rc.xml does it too),
`owbox --exit` exits. `owbox --msg` runs actions and lists windows, see
`man owbox`.

## Documentation

- [docs/GUIDE.md](docs/GUIDE.md): a user guide, with the programs that make
  a complete desktop with owbox
- [docs/CONFIG.md](docs/CONFIG.md): the `<owbox>` settings and the control
  socket
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md): differences from Openbox
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how Openbox's code was
  brought to Wayland, a map of the source
- [docs/DECISIONS.md](docs/DECISIONS.md): design decisions and why
- [docs/WORKAROUNDS.md](docs/WORKAROUNDS.md): workarounds for other
  projects' bugs, and where to report them
- [docs/ROADMAP.md](docs/ROADMAP.md): what's next
- [AGENTS.md](AGENTS.md): how to work on owbox (building, testing,
  documenting, releasing), for people and AI agents

## License

GPL-2.0-or-later, like Openbox, whose code much of this is. Copyright Dana
Jansens, Mikael Magnusson and the Openbox contributors, and the owbox
contributors. owbox is not affiliated with the Openbox project; the
Openbox name is used only to say what owbox is based on and compatible
with.
