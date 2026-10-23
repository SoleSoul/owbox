# owbox configuration reference

Most of this can be set in **owbox Settings** (`owbox-settings`); this is
the reference for the file it edits.

owbox reads Openbox's `rc.xml` and `menu.xml`; everything in Openbox's
documentation (<http://openbox.org/wiki/Help:Configuration>) applies. This
file describes only what owbox adds.

## Where the files are

Each file is looked for in these directories, and the first one found is
used:

1. `~/.config/owbox/`
2. `~/.config/openbox/`
3. `owbox/`, then `openbox/`, under each of `$XDG_CONFIG_DIRS` (by default
   `/etc/xdg`)

So an existing Openbox setup keeps working, and a `~/.config/owbox/` copy
can diverge from it. The files are:

- `rc.xml`, the configuration. It is reloaded when it changes, unless
  `<reloadOnChange>` is off.
- `menu.xml`, the menus, as in Openbox.
- `autostart`, a shell script run at startup. Only the owbox directories
  are used for it, since Openbox autostart scripts usually start X programs.
- `environment`, shell variable assignments read by `owbox-session`.

`--config-file FILE` uses FILE as rc.xml.

## The `<owbox>` section

All of it is optional. Openbox ignores the section, so an rc.xml with it
still works in Openbox.

```xml
<owbox>
  <keyboard>
    <layout>us,il</layout>              <!-- XKB, like setxkbmap -->
    <variant></variant>
    <options>grp:alt_shift_toggle</options>
    <model></model>
    <rules></rules>
    <repeatRate>25</repeatRate>         <!-- keys per second -->
    <repeatDelay>600</repeatDelay>      <!-- milliseconds -->
    <numlock>no</numlock>               <!-- turn num lock on at start -->
  </keyboard>
```

Without a layout, the `XKB_DEFAULT_*` environment variables are used, or the
system default.

```xml
  <libinput>
    <device match="touchpad">
      <tap>yes</tap>
      <naturalScroll>yes</naturalScroll>
      <disableWhileTyping>yes</disableWhileTyping>
      <middleEmulation>no</middleEmulation>
      <leftHanded>no</leftHanded>
      <drag>yes</drag>
      <dragLock>no</dragLock>
      <accelSpeed>0.0</accelSpeed>          <!-- -1.0 to 1.0 -->
      <accelProfile>adaptive</accelProfile> <!-- flat, adaptive -->
      <scrollMethod>twofinger</scrollMethod><!-- twofinger, edge, button, none -->
      <clickMethod>clickfinger</clickMethod><!-- clickfinger, buttonareas, none -->
      <tapButtonMap>lrm</tapButtonMap>      <!-- lrm, lmr -->
      <sendEvents>yes</sendEvents>          <!-- yes, no, disabledOnExternalMouse -->
    </device>
  </libinput>
```

`match` is `touchpad`, `pointer`, `*`, or a pattern (with `*` and `?`) for
the device's name. Every `<device>` that matches a device applies, and later
ones override earlier ones. Settings that are left out keep libinput's
default.

```xml
  <outputs>
    <output name="DP-1">
      <enabled>yes</enabled>
      <mode>2560x1440@144</mode>       <!-- WxH or WxH@Hz -->
      <scale>1</scale>
      <position x="0" y="0"/>
      <transform>normal</transform>    <!-- normal, 90, 180, 270, flipped,
                                            flipped-90, -180, -270 -->
      <adaptiveSync>no</adaptiveSync>
    </output>
  </outputs>
```

Monitors that aren't listed use their preferred mode at scale 1, placed to
the right of the others. Tools using wlr-output-management (`wlr-randr`,
`kanshi`, `wdisplays`) can change monitors at run time.

```xml
  <cursor>
    <theme>Adwaita</theme>
    <size>24</size>
  </cursor>
  <icons>
    <theme>Papirus</theme>
  </icons>
```

The cursor theme defaults to `XCURSOR_THEME`/`XCURSOR_SIZE`, then GNOME's
`cursor-theme` setting. The icon theme (used for window and menu icons)
defaults to GNOME's `icon-theme` setting, then GTK 3's settings.ini, then
Adwaita. owbox exports `XCURSOR_THEME` and `XCURSOR_SIZE` so that
applications draw the same cursor. Usually these are left out, and owbox
Settings' Applications page sets the themes for owbox and the applications
together.

```xml
  <decorations>auto</decorations>
```

- `auto` (the default): owbox decorates windows unless they insist on
  drawing their own decorations, as GTK 4/libadwaita applications do.
- `server`: owbox asks every window to let it decorate.
- `client`: windows decorate themselves.

```xml
  <background>#303030</background>
```

The color of the desktop when no wallpaper program is running, any color
Openbox themes accept.

```xml
  <idle>
    <screenOff>10</screenOff>   <!-- minutes, 0 for never -->
    <lock>yes</lock>
    <lockCommand></lockCommand>
  </idle>
```

The monitors turn off after `screenOff` minutes without input, unless a
visible window asks to keep them on (video players and calls do, with the
idle-inhibit protocol), and turn on with the next input. A monitor that is
off keeps its place and its windows. With `lock` on, the screen is locked
when the monitors turn off, before suspending, and when logind is asked to
(`loginctl lock-session`, Lock Screen in the menu). owbox runs the screen
locker `lockCommand`, or the first one installed of `swaylock -f`,
`gtklock -d`, `waylock -fork-on-lock` and `hyprlock`; a command for it
should only return (or fork) once the screen is locked. Set `screenOff` to
0 and `lock` to no to use swayidle instead.

```xml
  <panels>
    <autoHide>yes</autoHide>
    <dim>40</dim>              <!-- percent, 0 for none -->
  </panels>
```

Panels are layer-shell bars along an edge of the screen (waybar, and the
like), in the top or bottom layer; notifications and launchers aren't. The
`TogglePanels` action (tapping Super, in the default rc.xml) brings them
above all the windows, which are dimmed by `dim` percent, and runs a
launcher. It ends with Escape, a click outside the panels and the
launcher, switching to a window (the taskbar), the launcher closing, or
TogglePanels again, and the launcher is closed with it. With `autoHide`
the panels are hidden the rest of the time, and take no space from the
windows. Without it they stay where they are, and TogglePanels still raises
them above the dimmed windows.

`/usr/share/owbox/examples` has a waybar configuration made for this (a
thin bar at the top, and the windows in the middle of the bottom) and a
matching fuzzel one.

```xml
  <scale>1.5</scale>
```

How much larger than the theme says owbox draws window frames, menus,
Alt-Tab and the other popups, with their fonts: `1.5` or `150%` is half
again as large, from 0.5 to 4. Applications keep their size, while an
output's `<scale>` makes everything on that monitor larger. The theme's
bevels and borders get thicker by whole pixels, and its buttons (bitmaps,
pixel art) are drawn at the nearest whole number of times their size, so
they stay crisp: at 125% they keep their size, from 150% they double.
Gradients and text are sharp at any scale.

```xml
  <placement>cascade</placement>
```

How Openbox's `Smart` placement (`<policy>` in `<placement>`) places new
windows that don't have a position of their own (from per-application
settings, or asked for by an X application):

- `cascade` (the default): in the middle of the monitor, on top of the
  other windows. Where a new window would hide a whole window that can
  still be seen, it moves to that window's top left corner, down and to
  the right by a title bar, and so on, so that a stack of windows of one
  size shows every title bar. When there is no room for that, it stays in
  the middle. This is KDE's default placement (Centered) and labwc's
  (cascade).
- `openbox`: Openbox's, the free area with the least overlap, in its
  middle with `<center>`. When there is no free area, a window can end up
  in a corner.

Dialogs go over their parent window, and `UnderMouse` places windows under
the mouse, with both.

```xml
  <snapping enabled="yes">
    <range>10</range>          <!-- pixels from the edge -->
    <cornerRange>60</cornerRange>
    <topMaximize>yes</topMaximize>
    <preview>yes</preview>
    <dragUnmaximize>yes</dragUnmaximize>
  </snapping>
```

Dropping a window (moved with the mouse) at the left or right edge of a
monitor makes it fill that half, and dropping it at a corner fills that
quarter. `cornerRange` is how far from a corner along the edge counts as the
corner. The top edge maximizes, if `topMaximize` is on. Edges between two
monitors don't snap. With `preview` on, owbox shows where the window will
go. With `dragUnmaximize` on, dragging a maximized or snapped window
restores its size under the pointer.

```xml
  <desktopLayout rows="2" columns="2" orientation="horizontal"
                 corner="topleft"/>
```

This is how the desktops are arranged, for the desktop switching popup and
for `GoToDesktop` with directions. In Openbox a pager sets this layout.

```xml
  <reloadOnChange>yes</reloadOnChange>
  <allowShortcutsInhibit>yes</allowShortcutsInhibit>
  <xwayland>lazy</xwayland>
</owbox>
```

- `reloadOnChange` reconfigures owbox when rc.xml is saved.
- `allowShortcutsInhibit` lets applications that ask (virtual machines,
  remote desktops) receive all keys while focused. owbox's key bindings stop
  working until another window is focused.
- `xwayland` sets when X applications can run: `lazy` starts Xwayland when
  the first one connects, `yes` starts it with owbox, `no` turns X
  applications off. It is read only at startup.

## Actions owbox adds

```xml
<action name="SnapToEdge"><direction>west</direction></action>
<action name="Unsnap"/>
```

```xml
<action name="TogglePanels"><launcher>owbox-launcher</launcher></action>
```

`TogglePanels` shows the panels and runs `launcher`, or hides them (see
`<panels>`). `owbox-launcher` runs `$OWBOX_LAUNCHER`, or the first
installed of fuzzel, wofi, rofi, tofi and bemenu.

```xml
<keybind key="Super_L" tap="yes">
```

A key binding with `tap="yes"` runs when its key is released, if no other
key or button was pressed while it was down. It is meant for a modifier
alone: Super tapped runs the binding, while Super+E and Super+drag do what
they always did. (Openbox runs a binding of a modifier when it is pressed,
which is still what happens without `tap`.)

`SnapToEdge` takes the direction `north`, `south`, `east`, `west`,
`northeast`, `northwest`, `southeast` or `southwest` (or `up`, `down`,
`left`, `right`). The window fills that half or quarter of its monitor, but
north maximizes when `topMaximize` is on, like dropping a window at the top
edge. `Unsnap` restores a snapped window's size and position. The default rc.xml
binds Super+Left, Super+Right, Super+Up and Super+Down to these.

## Menus owbox adds

`<menu id="applications-menu"/>` in menu.xml is a submenu with the installed
applications (their desktop entries), by category. It updates when
applications are installed or removed. Applications that run in a terminal
are started with `owbox-terminal`, which uses `$TERMINAL` or the first
terminal emulator it finds.

Openbox's `client-list-menu` and `client-list-combined-menu` work as in
Openbox. Pipe menus work too.

## Default key bindings owbox adds

Besides Openbox's defaults and Super+arrows for snapping, the default rc.xml
binds Super+E to the file manager (`xdg-open ~`), the volume keys to
`owbox-volume` and the brightness keys to `owbox-brightness`, which change
the level (with `pactl`, for PulseAudio and PipeWire, and `brightnessctl`)
and show it in a notification, the media keys to `playerctl`, and Print
Screen to `owbox-screenshot`: Print for the whole screen, Alt+Print for the
focused window, Shift+Print for an area chosen with the mouse (`slurp`).
Screenshots go to `Pictures/Screenshots` and the clipboard.

An rc.xml of your own (or Openbox's) doesn't get new default bindings:
owbox Settings' Key Bindings page offers to add them.

Tapping Super runs `TogglePanels` with `owbox-launcher`.

The default menu's Leave submenu has Lock Screen, Suspend, Log Out, Restart
and Shut Down (the last two ask first).

## The session

`owbox-session` points `SSH_AUTH_SOCK` at the SSH agent systemd started
(GNOME's from gcr, or OpenSSH's `ssh-agent.socket`), as other desktops'
sessions do. owbox starts the systemd user target `owbox-session.target`,
which is bound to `graphical-session.target`, and stops it when it exits,
so services of the graphical session (portals, gvfs, panels run as
services) start and stop with it. The system autostart (`/etc/xdg/owbox/autostart`, run before
the user's) starts an installed polkit agent, which asks for passwords when
applications need administrator rights.

When owbox runs on a console (`owbox-session` from a virtual console),
the messages of the programs it starts, and its own, go to the systemd
journal instead of the console, as with a display manager. They are under
the identifier `owbox`: `journalctl --user -t owbox -b`, or
`journalctl -t owbox -b`. Programs that lose their connection when owbox
exits say so there.

The keyring (gnome-keyring, or anything providing
`org.freedesktop.secrets`) starts through D-Bus when an application first
needs it. Logging in on a text console doesn't unlock it, unless
`pam_gnome_keyring.so` is in `/etc/pam.d/login`; otherwise it asks for its
password the first time. Chromium and Electron applications choose their
password store by the desktop's name and don't know owbox, so they keep
passwords unencrypted and say there is no keyring. Start them with
`--password-store=gnome-libsecret` (Chromium reads its options from
`~/.config/chromium-flags.conf`).

## The control socket

owbox listens on `$XDG_RUNTIME_DIR/owbox-$WAYLAND_DISPLAY.sock` and exports
the path as `$OWBOX_SOCK`. `owbox --msg COMMAND` sends one command and
prints the reply. The protocol is one line per command, and the reply ends
with an empty line. The reply's first line is `ok` or `error <message>`.

| Command | Does |
|---|---|
| `reconfigure` | reloads the configuration (also `owbox --reconfigure`) |
| `restart` | the same as reconfigure |
| `exit` | exits (also `owbox --exit`) |
| `action <xml>` | runs actions, written as in rc.xml, on the focused window |
| `windows` | one JSON object per window, bottom to top |
| `desktops` | `{"current": n, "names": [...]}` |
| `outputs` | one JSON object per monitor: name, make, model, enabled, power (false while turned off), x, y, width, height, refresh (mHz), scale, transform, modes |
| `config-file` | the path of the rc.xml in use |
| `choose-share [monitors] [windows]` | shows the menu of monitors and/or windows to share (both when neither is given), and replies with `Monitor: <name>` or `Window: <identifier>`, or nothing when cancelled |

Examples:

    owbox --msg 'action <action name="GoToDesktop"><to>2</to></action>'
    owbox --msg 'action <action name="Execute"><command>foot</command></action>'
    owbox --msg windows | jq -r .title

A window object has `id`, `title`, `app_id`, `identifier` (its
ext-foreign-toplevel identifier, for `grim -T`), `desktop` (-1 is all
desktops), `x`, `y` (of the frame), `width`, `height` (of the client),
`frame` (the frame's left, top, right and bottom sizes around the client),
`focused`, `iconic`, `maximized`, `fullscreen` and `shaded`.

## Environment

owbox sets these for the programs it runs, and passes them on to D-Bus and
systemd user services:
- `WAYLAND_DISPLAY`
- `DISPLAY`, when Xwayland is on
- `XDG_CURRENT_DESKTOP=owbox` and `XDG_SESSION_DESKTOP`, unless already set
- `XDG_SESSION_TYPE=wayland`
- `XCURSOR_THEME` and `XCURSOR_SIZE`
- `OWBOX_SOCK`
- `_JAVA_AWT_WM_NONREPARENTING=1`
- `QT_QPA_PLATFORMTHEME=gtk3`, unless already set, so that Qt applications
  follow the desktop's icon theme, light or dark preference and fonts (Qt's
  defaults are light with hardly any icons)

`/usr/share/xdg-desktop-portal/owbox-portals.conf` selects
xdg-desktop-portal-wlr for screenshots and screen sharing, and the GTK
portal for the rest. `/etc/xdg/xdg-desktop-portal-wlr/owbox` makes
xdg-desktop-portal-wlr ask with owbox's menu (`owbox --choose-share`, a
chooser of the dmenu type) what to share. The menu offers only what the
application asked for: monitors, windows or both. Screen sharing also needs
PipeWire's session manager (WirePlumber) running.
