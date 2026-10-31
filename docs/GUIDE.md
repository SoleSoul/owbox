# owbox

*A user guide in the form of an ArchWiki article, so it can become one.
Package names are Arch Linux's. Related articles:
[Openbox](https://wiki.archlinux.org/title/Openbox),
[Wayland](https://wiki.archlinux.org/title/Wayland),
[Waybar](https://wiki.archlinux.org/title/Waybar),
[Xwayland](https://wiki.archlinux.org/title/Xwayland).*

[owbox](https://github.com/SoleSoul/owbox) is a stacking
[Wayland](https://wiki.archlinux.org/title/Wayland) compositor based on the
code of [Openbox](https://wiki.archlinux.org/title/Openbox). It reads
Openbox's configuration files (`rc.xml`, `menu.xml`) and themes, and keeps
Openbox's window management behavior, on top of wlroots. It is an
independent project, not Openbox, and is not affiliated with the Openbox
developers.

## Installation

Install the [owbox](https://aur.archlinux.org/packages/owbox) package, or
[owbox-git](https://aur.archlinux.org/packages/owbox-git) for the
development version.

Optional dependencies add the parts of a desktop that owbox does not draw
itself, see [A complete desktop](#a-complete-desktop). For X applications
install `xorg-xwayland`. For the settings application, *owbox Settings*,
install `python-gobject` and `gtk4`.

## Starting

Choose the *owbox* session in a
[display manager](https://wiki.archlinux.org/title/Display_manager) that
supports Wayland sessions, or run `owbox-session` from a
[virtual console](https://wiki.archlinux.org/title/Linux_console).
`owbox-session` reads `~/.config/owbox/environment` first, like
*openbox-session* does, and points `SSH_AUTH_SOCK` at the
[SSH agent](https://wiki.archlinux.org/title/SSH_keys#SSH_agents) started
by systemd.

Running `owbox` inside another Wayland or X session opens it in a window,
which is useful for trying it out.

owbox starts the [user](https://wiki.archlinux.org/title/Systemd/User) unit
`owbox-session.target`, which is bound to `graphical-session.target`, so
user services that belong to the graphical session (for example
`waybar.service`) start and stop with it.

## Configuration

### owbox Settings

Run `owbox-settings` (also in owbox's root menu). It covers the theme, the
look of applications (GTK theme, icons, cursor, fonts, light or dark, and
the buttons and clicks of title bars that applications draw), window
behavior, monitors, desktops, keyboard layouts, mouse and touchpad, key
bindings, idle and locking, and autostart. Changes are saved to `rc.xml`
and applied immediately.

### Configuration files

owbox looks for each file in `~/.config/owbox/`, then `~/.config/openbox/`,
then `/etc/xdg/owbox/`, so an existing Openbox configuration is used as it
is. To give owbox its own copy:

    $ mkdir -p ~/.config/owbox
    $ cp /etc/xdg/owbox/{rc.xml,menu.xml,autostart,environment} ~/.config/owbox/

`rc.xml` is reloaded when it is saved. Settings that Openbox does not have
go in an `<owbox>` section, which Openbox ignores: keyboard layouts, input
devices, monitors, cursor and icon themes, window placement, snapping, idle
and locking, the size of owbox's decorations (`<scale>`), and panels. The
installed `/etc/xdg/owbox/rc.xml` documents each of them, and the
`owbox(1)` man page describes the rest.

### Autostart

Programs to run at login go in `~/.config/owbox/autostart`, a shell script.
Applications in the
[XDG Autostart](https://wiki.archlinux.org/title/XDG_Autostart) directories
are started too. owbox does not run `~/.config/openbox/autostart`, because
it usually starts X programs that have Wayland replacements; owbox
Settings' *Autostart* page lists its commands with what replaces each one.

### Migrating from Openbox

- Key bindings, mouse bindings, menus, per-application settings and themes
  keep working.
- X tools in an Openbox setup need Wayland replacements: a panel such as
  *tint2* becomes [Waybar](https://wiki.archlinux.org/title/Waybar),
  *nitrogen* or *feh* becomes `swaybg`, *setxkbmap* and *xinput* become
  owbox Settings' keyboard and mouse pages, *xrandr* its displays page,
  *xscreensaver* and *xset* are replaced by owbox's idle settings with
  `swaylock`.
- owbox's own default key bindings (media keys, Print Screen, tapping
  Super) are not in an Openbox `rc.xml`; owbox Settings' *Key Bindings*
  page offers to add them.
- *obconf* is replaced by owbox Settings.

## A complete desktop

owbox manages windows, draws their frames, menus and the Alt+Tab switcher,
turns the monitors off when idle, and binds the media keys and Print
Screen. The rest of a desktop is up to other programs. These fit owbox
best; owbox also works with any alternative that supports the same Wayland
protocols. (Packages are in the official repositories unless marked AUR.)

| Purpose | Recommended | Alternatives | Notes |
|---|---|---|---|
| Panel, taskbar, tray | `waybar` | | `/usr/share/owbox/examples/waybar` has a configuration made for owbox. Enable `waybar.service` to start it with the session. |
| Launcher | `fuzzel` | `wofi`, `rofi` | Tapping Super shows it, see [Panels on Super](#panels-on-super). `/usr/share/owbox/examples/fuzzel` has a matching configuration. |
| Terminal | `foot` | `alacritty` | The menu's *Terminal* runs `$TERMINAL` or the first one installed. |
| Wallpaper | `swaybg` | | Without one, owbox fills the desktop with `<owbox><background>`. |
| Notifications | `mako` | `swaync`, `dunst` | Any daemon that provides `org.freedesktop.Notifications` works. The volume and brightness keys show their level through it. |
| Screen locker | `swaylock` | `gtklock` | owbox runs it when the monitors turn off, before suspending, and from the menu's *Lock Screen*. No *swayidle* is needed. |
| Authentication agent | `polkit-gnome` | `lxqt-policykit` | owbox starts an installed one, see [Polkit](https://wiki.archlinux.org/title/Polkit#Authentication_agents). |
| Screenshots | `grim`, `slurp` | | Print Screen, Alt+Print Screen (the window) and Shift+Print Screen (an area) run `owbox-screenshot`, which uses them. |
| Clipboard | `wl-clipboard`, `wl-clip-persist` | `cliphist` | *wl-clip-persist* keeps copied text after the program it came from closes. |
| Volume, media and brightness keys | `libpulse` (*pactl*), `playerctl`, `brightnessctl` | | The default bindings use them. |
| Audio settings | `pavucontrol` | | |
| Network | `network-manager-applet` | `iwgtk` (AUR) | For [NetworkManager](https://wiki.archlinux.org/title/NetworkManager) or [iwd](https://wiki.archlinux.org/title/Iwd). |
| Bluetooth | `blueman` | | |
| Night light | `wlsunset` | `gammastep` | |
| Monitor profiles | `kanshi` | `wdisplays` | owbox Settings sets the monitors; *kanshi* switches between setups automatically. |
| File manager | `thunar` | `pcmanfm-qt`, `nautilus` | Super+E opens the home folder with the default one. |
| Screen sharing | `xdg-desktop-portal-wlr`, `wireplumber` | | owbox shows its own menu to choose a monitor or a window. *WirePlumber* is needed even when [PulseAudio](https://wiki.archlinux.org/title/PulseAudio) handles the sound. |
| Other portals (file choosers) | `xdg-desktop-portal-gtk` | | |
| Keyring | `gnome-keyring` | | See [GNOME/Keyring](https://wiki.archlinux.org/title/GNOME/Keyring). |

## Tips and tricks

### Panels on Super

Tapping Super (pressing and releasing it alone) shows the panels and the
launcher over the dimmed windows, until Escape, a click elsewhere, or
switching to a window. To keep the panels hidden the rest of the time, so
windows have the whole screen, in `~/.config/owbox/rc.xml`:

```xml
<owbox>
  <panels>
    <autoHide>yes</autoHide>
    <dim>40</dim>
  </panels>
</owbox>
```

The binding is an ordinary key binding with the `tap` attribute:

```xml
<keybind key="Super_L" tap="yes">
  <action name="TogglePanels"><launcher>owbox-launcher</launcher></action>
</keybind>
```

### Larger decorations

`<owbox><scale>` draws window frames, menus and popups larger than the
theme without making applications larger, which suits Openbox themes on
high resolution monitors. owbox Settings sets it under *Appearance*.

### Scripting

`owbox --msg` sends commands to the running owbox: Openbox actions, and
lists of the windows, desktops and monitors as JSON. For example:

    $ owbox --msg 'action <action name="GoToDesktop"><to>2</to></action>'
    $ owbox --msg windows

## Troubleshooting

### Screen sharing shows nothing

Make sure [WirePlumber](https://wiki.archlinux.org/title/WirePlumber) is
running; without a session manager, PipeWire does not connect the streams.

### X panels and pagers show no windows

X programs such as *tint2* cannot see Wayland windows. Use a Wayland panel
such as [Waybar](https://wiki.archlinux.org/title/Waybar).

### Chromium or Electron programs find no keyring

Chromium and Electron choose their password store by the desktop's name,
and do not know owbox. Start them with `--password-store=gnome-libsecret`,
see [Chromium](https://wiki.archlinux.org/title/Chromium#Force_a_password_store).
When logging in on a text console, add *pam_gnome_keyring* to
`/etc/pam.d/login` to unlock the keyring, see
[GNOME/Keyring](https://wiki.archlinux.org/title/GNOME/Keyring#PAM_step).

### Logging out leaves services failed

owbox stops `owbox-session.target` when it exits and clears failed user
units when it starts. If owbox crashed, run
`systemctl --user reset-failed`.

## See also

- [CONFIG.md](CONFIG.md), the reference of the `<owbox>` settings, and
  [COMPATIBILITY.md](COMPATIBILITY.md), the differences from Openbox
- [Openbox](https://wiki.archlinux.org/title/Openbox) on the ArchWiki
- [Openbox configuration documentation](http://openbox.org/wiki/Help:Configuration),
  which applies to owbox
