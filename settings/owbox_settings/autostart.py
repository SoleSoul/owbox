# autostart.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""owbox's autostart script (~/.config/owbox/autostart), one program a
line, and what to do with the programs of an Openbox autostart."""

import os
import re
from gettext import gettext as _

from .rcxml import config_home

# programs made for X that Openbox setups start, and what does their job
# under Wayland
X_PROGRAMS = {
    "xcompmgr": _("not needed, owbox composites itself"),
    "compton": _("not needed, owbox composites itself"),
    "picom": _("not needed, owbox composites itself"),
    "nitrogen": _("for a wallpaper use swaybg"),
    "feh": _("for a wallpaper use swaybg"),
    "hsetroot": _("for a wallpaper use swaybg, or Background in owbox's "
                  "settings"),
    "xsetroot": _("use the background color in owbox's settings"),
    "tint2": _("an X panel, use waybar"),
    "lxpanel": _("an X panel, use waybar"),
    "xfce4-panel": _("use waybar, or a Wayland build of xfce4-panel"),
    "xset": _("X only, screen blanking is swayidle's job"),
    "xscreensaver": _("use swayidle and swaylock"),
    "light-locker": _("use swayidle and swaylock"),
    "setxkbmap": _("set the keyboard in owbox's Keyboard settings"),
    "numlockx": _("turn Num Lock on in owbox's Keyboard settings"),
    "xrandr": _("set the monitors in owbox's Displays settings"),
    "xmodmap": _("set the keyboard in owbox's Keyboard settings"),
    "xbindkeys": _("use owbox's Key Bindings"),
    "unclutter": _("X only"),
    "xinput": _("set the mouse and touchpad in owbox's settings"),
    "synclient": _("set the touchpad in owbox's settings"),
}


def user_path():
    return os.path.join(config_home(), "owbox", "autostart")


def openbox_path():
    return os.path.join(config_home(), "openbox", "autostart")


def _program(cmd):
    m = re.match(r"\s*(?:\(\s*)?(?:[A-Z_]+=\S+\s+)*(\S+)", cmd)
    return os.path.basename(m.group(1)) if m else ""


def _read(path):
    try:
        with open(path) as f:
            return f.read().splitlines()
    except OSError:
        return []


def _command_lines(lines):
    """The lines that run something (not comments or blank)"""
    return [l for l in lines if l.strip() and not l.strip().startswith("#")]


def _plain(line):
    """The command of a line, without the & that puts it in the
    background"""
    line = line.strip()
    return line[:-1].rstrip() if line.endswith("&") else line


def commands():
    return [_plain(l) for l in _command_lines(_read(user_path()))]


def add(cmd):
    lines = _read(user_path())
    if not lines:
        lines = ["# Programs to run when owbox starts, written by owbox "
                 "Settings."]
    if cmd in commands():
        return
    lines.append(cmd + " &")
    _write(lines)


def remove(cmd):
    lines = [l for l in _read(user_path())
             if not (l.strip() and not l.strip().startswith("#") and
                     _plain(l) == cmd)]
    _write(lines)


def _write(lines):
    path = user_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "w") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(path + ".tmp", path)


def openbox_commands():
    """The commands of the Openbox autostart, with a note for those made
    for X"""
    out = []
    for line in _command_lines(_read(openbox_path())):
        cmd = _plain(line)
        prog = _program(cmd)
        out.append((cmd, X_PROGRAMS.get(prog)))
    return out
