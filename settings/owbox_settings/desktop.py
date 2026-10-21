# desktop.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The look of applications: GTK's theme, the icon and cursor themes, the
fonts and the light or dark preference.

On Wayland, GTK applications (and owbox) read them from GSettings
(org.gnome.desktop.interface), and libadwaita, Electron and Chromium
applications read the preference through the settings portal, which reads
GSettings too.  GTK applications running on X (Xwayland) read GTK's
settings.ini instead, so a change is written to both.  Qt applications
have their own settings (qt5ct, qt6ct); the icon theme is written there
when they are used."""

import os
import re

from gi.repository import Gio, GLib

from .rcxml import config_home

SCHEMA = "org.gnome.desktop.interface"

# key: (GSettings key, settings.ini key, default)
KEYS = {
    "gtk-theme": ("gtk-theme", "gtk-theme-name", "Adwaita"),
    "icon-theme": ("icon-theme", "gtk-icon-theme-name", "Adwaita"),
    "cursor-theme": ("cursor-theme", "gtk-cursor-theme-name", "Adwaita"),
    "cursor-size": ("cursor-size", "gtk-cursor-theme-size", 24),
    "font-name": ("font-name", "gtk-font-name", "Sans 10"),
    "monospace-font-name": ("monospace-font-name", None, "Monospace 10"),
    "color-scheme": ("color-scheme", None, "default"),
}

COLOR_SCHEMES = ("default", "prefer-dark", "prefer-light")


def _gsettings():
    src = Gio.SettingsSchemaSource.get_default()
    if src is None or src.lookup(SCHEMA, True) is None:
        return None
    return Gio.Settings.new(SCHEMA)


def _settings_ini(version):
    return os.path.join(config_home(), "gtk-%s" % version, "settings.ini")


def _read_ini(path):
    kf = GLib.KeyFile()
    try:
        kf.load_from_file(path, GLib.KeyFileFlags.KEEP_COMMENTS)
    except GLib.Error:
        pass
    return kf


class DesktopSettings:
    def __init__(self):
        self.gs = _gsettings()

    def available(self):
        """GSettings has the desktop's settings (gsettings-desktop-schemas
        is installed)"""
        return self.gs is not None

    def get(self, key):
        gkey, ini_key, default = KEYS[key]
        if self.gs is not None and self.gs.get_property("settings-schema") \
                .has_key(gkey):
            v = self.gs.get_value(gkey).unpack()
            if v not in ("", 0):
                return v
        if ini_key:
            kf = _read_ini(_settings_ini("3.0"))
            try:
                v = kf.get_string("Settings", ini_key)
                return int(v) if isinstance(default, int) else v
            except (GLib.Error, ValueError):
                pass
        return default

    def set(self, key, value):
        gkey, ini_key, default = KEYS[key]
        if self.gs is not None and self.gs.get_property("settings-schema") \
                .has_key(gkey):
            if isinstance(default, int):
                self.gs.set_int(gkey, int(value))
            elif gkey == "color-scheme":
                self.gs.set_enum(gkey, COLOR_SCHEMES.index(value))
            else:
                self.gs.set_string(gkey, value)
            Gio.Settings.sync()
        # all of them, so that applications on X match from the first
        # change
        for version in ("3.0", "4.0"):
            kf = _read_ini(_settings_ini(version))
            for k, (g, ini, d) in KEYS.items():
                v = value if k == key else self.get(k)
                if ini:
                    kf.set_string("Settings", ini, str(v))
                elif k == "color-scheme":
                    kf.set_string("Settings",
                                  "gtk-application-prefer-dark-theme",
                                  "true" if v == "prefer-dark" else "false")
            path = _settings_ini(version)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            kf.save_to_file(path)
        if key == "icon-theme":
            for qt in ("qt5ct", "qt6ct"):
                set_qt_icon_theme(qt, value)


def qt_config(qt):
    """The qt5ct or qt6ct configuration file, if it is used"""
    path = os.path.join(config_home(), qt, qt + ".conf")
    return path if os.path.isfile(path) else None


def set_qt_icon_theme(qt, theme):
    """Change icon_theme in [Appearance], keeping the rest of the file as it
    is (Qt writes it, GLib's key files would change its quoting)"""
    path = qt_config(qt)
    if not path:
        return
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    section = None
    done = False
    for i, line in enumerate(lines):
        m = re.match(r"\s*\[(.*)\]\s*$", line)
        if m:
            if section == "Appearance" and not done:
                # at the end of [Appearance], before the blank lines
                while i > 0 and not lines[i - 1].strip():
                    i -= 1
                lines.insert(i, "icon_theme=" + theme)
                done = True
                break
            section = m.group(1)
        elif section == "Appearance" and re.match(r"\s*icon_theme\s*=", line):
            lines[i] = "icon_theme=" + theme
            done = True
            break
    if not done:
        while lines and not lines[-1].strip():
            lines.pop()
        if section != "Appearance":
            lines += ["", "[Appearance]"]
        lines += ["icon_theme=" + theme, ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


def gtk_themes():
    """The installed GTK 3 themes, and GTK's own"""
    dirs = [os.path.join(GLib.get_user_data_dir(), "themes"),
            os.path.expanduser("~/.themes")]
    dirs += [os.path.join(d, "themes") for d in GLib.get_system_data_dirs()]
    found = {"Adwaita", "HighContrast"}
    for d in dirs:
        try:
            for name in os.listdir(d):
                if os.path.isfile(os.path.join(d, name, "gtk-3.0",
                                               "gtk.css")):
                    found.add(name)
        except OSError:
            pass
    return sorted(found, key=str.lower)
