# autostart.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""owbox's autostart script (~/.config/owbox/autostart), one program a
line; the applications of XDG autostart (~/.config/autostart) that owbox
runs; and what to do with the programs of an Openbox autostart."""

import os
import re
import shutil
from gettext import gettext as _

import gi
gi.require_version("GLib", "2.0")
from gi.repository import GLib  # noqa: E402

from .rcxml import config_home, config_dirs  # noqa: E402

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
    "xset": _("not needed, owbox turns the monitors off when idle "
              "(Displays settings)"),
    "xscreensaver": _("not needed, install swaylock and owbox locks the "
                      "screen with it"),
    "light-locker": _("not needed, install swaylock and owbox locks the "
                      "screen with it"),
    "xss-lock": _("not needed, owbox locks the screen before suspending"),
    "xautolock": _("not needed, owbox locks the screen when idle"),
    "xfce4-power-manager": _("X only, owbox turns the monitors off when "
                             "idle"),
    "volumeicon": _("an X tray icon, the volume keys work without it"),
    "polkit-gnome-authentication-agent-1": _("not needed, owbox starts a "
                                             "polkit agent"),
    "lxpolkit": _("not needed, owbox starts a polkit agent"),
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


# ---- XDG autostart, run by owbox like openbox-xdg-autostart does ----

GROUP = "Desktop Entry"


class Entry:
    def __init__(self, filename, path, name, on):
        self.filename = filename
        self.path = path
        self.name = name
        self.on = on


def _xdg_user_dir():
    return os.path.join(config_home(), "autostart")


def _desktops():
    return [d.lower() for d in
            (os.environ.get("XDG_CURRENT_DESKTOP") or "owbox").split(":")]


def _load(path):
    kf = GLib.KeyFile()
    try:
        kf.load_from_file(path, GLib.KeyFileFlags.KEEP_COMMENTS |
                          GLib.KeyFileFlags.KEEP_TRANSLATIONS)
    except GLib.Error:
        return None
    return kf if kf.has_group(GROUP) else None


def _get(kf, key, kind="string"):
    try:
        return getattr(kf, "get_" + kind)(GROUP, key)
    except GLib.Error:
        return None


def _for_owbox(kf):
    """Whether owbox runs the entry when it isn't Hidden: meant for this
    desktop, and installed"""
    mine = _desktops()
    only = _get(kf, "OnlyShowIn", "string_list")
    if only is not None and not any(d.lower() in mine for d in only):
        return False
    nots = _get(kf, "NotShowIn", "string_list")
    if nots is not None and any(d.lower() in mine for d in nots):
        return False
    tryexec = _get(kf, "TryExec")
    if tryexec:
        if os.path.isabs(tryexec):
            return os.access(tryexec, os.X_OK)
        return shutil.which(tryexec) is not None
    return True


def xdg_entries():
    """The applications owbox starts from XDG autostart, and those turned
    off (Hidden), sorted by name.  A file in the user's directory replaces
    the system's one with the same name."""
    seen = {}
    for d in config_dirs():
        d = os.path.join(d, "autostart")
        try:
            names = sorted(os.listdir(d))
        except OSError:
            continue
        for fn in names:
            if not fn.endswith(".desktop") or fn in seen:
                continue
            path = os.path.join(d, fn)
            seen[fn] = None
            kf = _load(path)
            if kf is None or not _for_owbox(kf):
                continue
            name = _get(kf, "Name", "locale_string") or fn[:-8]
            seen[fn] = Entry(fn, path, name,
                             not _get(kf, "Hidden", "boolean"))
    return sorted((e for e in seen.values() if e),
                  key=lambda e: e.name.lower())


def set_xdg_on(entry, on):
    """Turn an application on or off, as the specification says: with
    Hidden in a file of the same name in the user's directory"""
    user = os.path.join(_xdg_user_dir(), entry.filename)
    kf = _load(entry.path)
    if kf is None:
        return
    if on:
        try:
            kf.remove_key(GROUP, "Hidden")
        except GLib.Error:
            pass
    else:
        kf.set_boolean(GROUP, "Hidden", True)
    os.makedirs(_xdg_user_dir(), exist_ok=True)
    kf.save_to_file(user)
    entry.path = user
    entry.on = on
    # a copy that is the same as the system's isn't needed
    for d in config_dirs()[1:]:
        system = os.path.join(d, "autostart", entry.filename)
        if os.path.isfile(system):
            sk = _load(system)
            if sk is not None and sk.to_data()[0] == kf.to_data()[0]:
                os.remove(user)
                entry.path = system
            break
