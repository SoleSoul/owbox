# Design decisions

Why owbox is the way it is. Newest decisions go at the end.

## The project's brief (2026-10)

The owner used Openbox for a long time and wanted it on Wayland, with
Openbox's goals: standards compliant, complete, sane defaults, light. It
should be a drop-in replacement where appropriate (the same config files
and themes, the same feel), and built on minimal, high quality libraries
that map well onto Openbox's structure. The brief also allowed challenging
that, and adding quality of life features or the features Openbox users
want most.

In the one decision session before implementation, the owner chose:

| Question | Choice |
|---|---|
| Base | **From scratch, faithful to Openbox**, not a fork of labwc |
| Configuration | **Drop-in plus extensions**: read Openbox's files, put additions where Openbox ignores them |
| Extras | **Snapping and tiling**, and **optional eye candy**, and asked for recommendations |
| Desktops | **Global, like Openbox** (not per monitor) |

## Recommendations made on the extras

Asked for a recommendation even after choosing, these were the
recommendations, in priority order:

1. **Snapping**: halves and quarters by dragging to edges and corners, top
   edge maximizes, keyboard snapping. This is the most requested window
   manager feature of the last decade. *Done.*
2. **A scripting interface**: Openbox users script with xdotool and wmctrl,
   which can't work on Wayland. A small socket that runs Openbox actions
   and lists windows covers that. *Done* (`owbox --msg`).
3. **An applications menu built in**: every Openbox setup needs a menu
   generator (obmenu-generator, xdg-menu). *Done* (`applications-menu`).
4. **Touchpad gestures bound to actions**, through rc.xml like mouse
   bindings. *Roadmap.*
5. **Eye candy**: rounded corners, shadows, animations. Off by default,
   since a light and fast window manager is the point of Openbox. *Roadmap.*

Tiling beyond snapping (automatic layouts) was not recommended: it changes
what Openbox is, and Openbox users who want it use a tiling compositor.

## Technical decisions

**Port Openbox's code, rather than reimplement its behavior.** Openbox's
"feel" is hundreds of small decisions in its code (placement, focus
stealing prevention, resistance, menu timing, Alt-Tab order). Reusing the
code is the only way to get them all right, and it's GPL-2.0-or-later like
owbox. Openbox modules stay recognizable, and X-specific parts are replaced
by small emulation layers (ObFWin, ObEvent, ObClientImpl).

**wlroots 0.20 with its scene graph.** It's the reference library for
stacking compositors (sway, labwc, wayfire), with the widest protocol
support, and its scene graph matches Openbox's X window tree. Smithay
(Rust) and libweston were ruled out: one means rewriting Openbox, the other
is a whole compositor's policy.

**Keep glib's main loop.** Openbox is built around it, so the Wayland event
loop is added to it as a GSource rather than the other way around.

**obrender into cairo.** Openbox's theme engine is ported to draw in memory
with cairo, and its text with pango. Themes look pixel identical, and
there's no new theme format.

**Restart is reconfigure.** A Wayland compositor that execs itself loses
all its clients.

**Decorations: decorate unless the client insists.** xdg-decoration
clients get server-side decorations unless they ask for client-side ones.
GTK 3 uses KDE's server-decoration protocol, which defaults to server-side.
Clients with neither (GTK 4) are treated like X windows whose Motif hints
ask for no decorations. That is the nearest equivalent of Openbox's X
behavior.

**Configuration lookup goes directory by directory**: `~/.config/owbox`,
`~/.config/openbox`, then the system's `owbox`, `openbox`. A user's Openbox
config beats the system owbox defaults, which is what drop-in means.

**Reload when rc.xml changes.** Openbox relied on ObConf and editors
sending `openbox --reconfigure`. Watching the file makes ObConf (run
through Xwayland) and any editor work without that.

**X applications through wlroots' xwm**, rather than running Openbox's own
X code as the X window manager. Two window managers would disagree. wlroots'
xwm already reads every property Openbox needs, and xwayland.c maps them
back to Openbox's semantics.

**Pings are polled.** wlroots doesn't report pongs, so ping.c checks every
3 seconds whether the last ping is still unanswered. Openbox's timing (warn
after two missed intervals) is kept.

**Window groups become processes**, since Wayland has no window groups.

**Icon and cursor themes default to the desktop's** (GNOME/GTK settings),
since Openbox users usually set them for GTK and don't want to repeat it.
