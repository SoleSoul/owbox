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
when they are used.

Applications that draw their own title bar (GTK 4 and libadwaita ones)
take its buttons and what clicks on it do from the window manager's
settings (org.gnome.desktop.wm.preferences), which are made from owbox's
when the title bars follow owbox's (owbox-settings.ini)."""

import os
import re

from gi.repository import Gio, GLib

from .rcxml import config_home, q

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

WM_SCHEMA = "org.gnome.desktop.wm.preferences"

# Openbox's titleLayout letters and GTK's buttons; S (roll up) and D (all
# desktops) have none
TITLE_BUTTONS = {"N": "icon", "I": "minimize", "M": "maximize",
                 "C": "close"}

# Openbox's actions and the title bar actions GTK has
TITLEBAR_ACTIONS = {"ToggleMaximize": "toggle-maximize",
                    "Iconify": "minimize", "Lower": "lower",
                    "ShowMenu": "menu"}

# key: (GSettings key, settings.ini key)
TITLEBAR_KEYS = {
    "button-layout": ("button-layout", "gtk-decoration-layout"),
    "double-click": ("action-double-click-titlebar",
                     "gtk-titlebar-double-click"),
    "middle-click": ("action-middle-click-titlebar",
                     "gtk-titlebar-middle-click"),
    "right-click": ("action-right-click-titlebar",
                    "gtk-titlebar-right-click"),
}


def _gsettings(schema=SCHEMA):
    src = Gio.SettingsSchemaSource.get_default()
    if src is None or src.lookup(schema, True) is None:
        return None
    return Gio.Settings.new(schema)


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


def button_layout(title_layout):
    """GTK's button layout for an Openbox titleLayout: the buttons before
    the title (L) on the left, the others on the right"""
    left, right = [], []
    side = left
    for c in title_layout:
        if c == "L":
            side = right
        elif c in TITLE_BUTTONS and TITLE_BUTTONS[c] not in left + right:
            side.append(TITLE_BUTTONS[c])
    return ",".join(left) + ":" + ",".join(right)


def _titlebar_action(rc, button, events):
    """What GTK should do for a click on the title bar: the first action
    GTK has of Openbox's binding of the button in the Titlebar context
    (the actions of all the contexts' bindings, as Openbox puts them
    together)"""
    actions = []
    for event in events:
        for context in rc.root.iter(q("context")):
            if "titlebar" not in context.get("name", "").lower().split():
                continue
            for mb in context.findall(q("mousebind")):
                if mb.get("button") == button and \
                        mb.get("action", "").lower() == event.lower():
                    actions += mb.findall(q("action"))
    for a in actions:
        name = a.get("name", "")
        name = {n.lower(): n for n in TITLEBAR_ACTIONS}.get(name.lower())
        if name == "ShowMenu":
            menu = a.find(q("menu"))
            if menu is None or (menu.text or "").strip() != "client-menu":
                continue
        if name:
            return TITLEBAR_ACTIONS[name]
    return "none"


def titlebar_settings(rc):
    """The window manager's settings made from an rc.xml"""
    return {"button-layout": button_layout(rc.get("theme/titleLayout",
                                                  "NLIMC")),
            "double-click": _titlebar_action(rc, "Left", ["DoubleClick"]),
            "middle-click": _titlebar_action(rc, "Middle",
                                             ["Press", "Click"]),
            "right-click": _titlebar_action(rc, "Right", ["Press", "Click"])}


def set_titlebar_settings(values):
    """Write the window manager's settings, to GSettings for applications
    on Wayland and to GTK's settings.ini for applications on X.  Only the
    changed ones are written."""
    gs = _gsettings(WM_SCHEMA)
    if gs is not None:
        keys = gs.get_property("settings-schema")
        for key, value in values.items():
            # the click actions are enums, which are kept as strings
            gkey = TITLEBAR_KEYS[key][0]
            if keys.has_key(gkey) and gs.get_string(gkey) != value:
                gs.set_string(gkey, value)
        Gio.Settings.sync()
    for version in ("3.0", "4.0"):
        path = _settings_ini(version)
        kf = _read_ini(path)
        changed = False
        for key, value in values.items():
            ini = TITLEBAR_KEYS[key][1]
            try:
                old = kf.get_string("Settings", ini)
            except GLib.Error:
                old = None
            if old != value:
                kf.set_string("Settings", ini, value)
                changed = True
        if changed:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            kf.save_to_file(path)


def _prefs_path():
    return os.path.join(config_home(), "owbox", "owbox-settings.ini")


def titlebars_follow():
    """Do applications' title bars follow owbox's (yes unless turned
    off)"""
    kf = _read_ini(_prefs_path())
    try:
        return kf.get_boolean("Applications", "TitleBarsFollowOwbox")
    except GLib.Error:
        return True


def set_titlebars_follow(follow):
    path = _prefs_path()
    kf = _read_ini(path)
    kf.set_boolean("Applications", "TitleBarsFollowOwbox", follow)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kf.save_to_file(path)


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
