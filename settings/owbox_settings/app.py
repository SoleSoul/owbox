# app.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""owbox Settings: the settings of rc.xml (Openbox's and owbox's) in a
window, saved as they are changed, like ObConf does."""

import os
import sys
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gio, GLib, Gtk  # noqa: E402

from .rcxml import (RcXml, find_config, running_config,  # noqa: E402
                    owbox_config_path, reconfigure)
from .keybinds import KeyBindings  # noqa: E402
from . import desktop  # noqa: E402
from .widgets import label, SAVE_DELAY  # noqa: E402
from .page_look import LookPages  # noqa: E402
from .page_windows import WindowsPages  # noqa: E402
from .page_input import InputPages  # noqa: E402
from .page_panels import PanelsPages  # noqa: E402
from .page_autostart import AutostartPages  # noqa: E402


APP_ID = "org.owbox.Settings"


class SettingsWindow(LookPages, WindowsPages, PanelsPages, InputPages,
                     AutostartPages, Gtk.ApplicationWindow):
    def __init__(self, app, path):
        super().__init__(application=app, title=_("owbox Settings"))
        self.set_default_size(820, 620)
        self.rc = RcXml(path)
        self.keys = KeyBindings(self.rc)
        self.save_id = 0
        self.desktop = desktop.DesktopSettings()
        self.desktop_id = 0

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.set_child(outer)
        outer.append(self.notice())

        body = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        body.set_vexpand(True)
        outer.append(body)
        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        side = Gtk.StackSidebar(stack=self.stack)
        side.set_size_request(180, -1)
        body.append(side)
        body.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        self.stack.set_hexpand(True)
        body.append(self.stack)

        pages = [("theme", _("Theme"), self.page_theme),
                 ("appearance", _("Appearance"), self.page_appearance),
                 ("applications", _("Applications"),
                  self.page_applications),
                 ("windows", _("Windows"), self.page_windows),
                 ("panels", _("Panels"), self.page_panels),
                 ("displays", _("Displays"), self.page_displays),
                 ("desktops", _("Desktops"), self.page_desktops),
                 ("keyboard", _("Keyboard"), self.page_keyboard),
                 ("mouse", _("Mouse & Touchpad"), self.page_mouse),
                 ("keys", _("Key Bindings"), self.page_keys),
                 ("autostart", _("Autostart"), self.page_autostart)]
        for name, title, build in pages:
            self.stack.add_titled(build(), name, title)
        # rc.xml may have changed since, by hand
        self.sync_titlebars()

    # ---- saving ----

    def changed(self):
        """Save soon, several quick changes (a slider) make one save"""
        if self.save_id:
            GLib.source_remove(self.save_id)
        self.save_id = GLib.timeout_add(SAVE_DELAY, self.save_now)

    def save_now(self):
        self.save_id = 0
        try:
            self.rc.save()
        except OSError as e:
            self.error(_("Unable to save %s: %s") % (self.rc.path, e))
            return False
        reconfigure()
        self.sync_titlebars()
        return False

    def error(self, text):
        d = Gtk.AlertDialog(message=text)
        d.show(self)

    # ---- where the settings are ----

    def notice(self):
        """Tell where the settings are saved, and offer a separate copy when
        they're Openbox's"""
        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(8)
        text = label("")
        text.set_hexpand(True)
        box.append(text)
        button = Gtk.Button()
        box.append(button)
        self.notice_box, self.notice_text, self.notice_button = \
            box, text, button
        button.connect("clicked", self.on_make_copy)
        self.update_notice()
        return box

    def update_notice(self):
        path = self.rc.path.replace(os.path.expanduser("~"), "~", 1)
        if not self.rc.is_user_file():
            self.notice_text.set_label(
                _("These are the system's default settings (%s). Your changes "
                  "will be saved in ~/.config/owbox.") % path)
            self.notice_button.set_visible(False)
        elif self.rc.is_openbox_file():
            self.notice_text.set_label(
                _("You're using your Openbox settings (%s). Openbox keeps "
                  "working with them, it ignores owbox's additions.") % path)
            self.notice_button.set_label(_("Use separate settings for owbox"))
            self.notice_button.set_visible(True)
        else:
            self.notice_text.set_label(_("Settings are saved in %s and apply "
                                         "right away.") % path)
            self.notice_button.set_visible(False)

    def on_make_copy(self, *args):
        self.rc.copy_to_owbox()
        reconfigure()
        self.update_notice()
        self.fill_defaults()

    def ensure_user_file(self):
        """The first change to the system's file is saved as the user's"""
        if not self.rc.is_user_file():
            self.rc.path = owbox_config_path()
            os.makedirs(os.path.dirname(self.rc.path), exist_ok=True)
            self.update_notice()

    def set(self, path, value):
        self.ensure_user_file()
        self.rc.set(path, value)
        self.changed()

    def set_bool(self, path, value):
        self.set(path, "yes" if value else "no")

    # ---- controls bound to rc.xml ----

    def switch(self, path, default):
        sw = Gtk.Switch(active=self.rc.get_bool(path, default))
        sw.connect("notify::active", lambda w, p: self.set_bool(path,
                                                                w.get_active()))
        return sw

    def spin(self, path, lo, hi, default, step=1):
        sp = Gtk.SpinButton.new_with_range(lo, hi, step)
        sp.set_value(self.rc.get_int(path, default))
        sp.connect("value-changed",
                   lambda w: self.set(path, str(int(w.get_value()))))
        return sp

    def dropdown(self, path, choices, default):
        """choices: [(value, label)]"""
        values = [c[0] for c in choices]
        dd = Gtk.DropDown.new_from_strings([c[1] for c in choices])
        cur = self.rc.get(path, default)
        lower = [v.lower() for v in values]
        if cur is not None and cur.lower() in lower:
            dd.set_selected(lower.index(cur.lower()))
        dd.connect("notify::selected", lambda w, p: self.set_or_unset(
            path, values[w.get_selected()]))
        return dd

    def scale_dropdown(self):
        """<owbox><scale>, as percentages"""
        values = ["", "1.1", "1.15", "1.2", "1.25", "1.35", "1.5", "1.75",
                  "2", "2.5", "3"]
        cur = self.rc.get("owbox/scale", "")
        try:
            num = float(cur.rstrip("%")) / (100 if "%" in cur else 1)
        except ValueError:
            num = 1.0
        nums = [1.0] + [float(v) for v in values[1:]]
        if num not in nums:
            # a scale written in rc.xml by hand
            values.append(cur)
            nums.append(num)
        dd = Gtk.DropDown.new_from_strings(
            ["%d%%" % round(n * 100) for n in nums])
        dd.set_selected(nums.index(num))
        dd.connect("notify::selected", lambda w, p: self.set_or_unset(
            "owbox/scale", values[w.get_selected()]))
        return dd

    def set_or_unset(self, path, value):
        if value == "":
            self.ensure_user_file()
            self.rc.unset(path)
            self.changed()
        else:
            self.set(path, value)


class SettingsApp(Gtk.Application):
    def __init__(self, path=None):
        super().__init__(application_id=APP_ID,
                         flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.path = path

    def do_startup(self):
        Gtk.Application.do_startup(self)
        # GTK 4.24 loses the styles of a theme kept in a gresource (like
        # WhiteSur) when it loads it again, as it does when the light or
        # dark preference changes, which this window changes: use GTK's own
        # theme, which follows the preference without that (libadwaita
        # applications look like it anyway).  See docs/WORKAROUNDS.md.
        Gtk.Settings.get_default().set_property("gtk-theme-name", "Adwaita")

    def do_activate(self):
        win = self.props.active_window
        if not win:
            path = self.path or running_config() or find_config()
            if path is None:
                d = Gtk.AlertDialog(message=_("No rc.xml found, is owbox "
                                              "installed?"))
                d.show(None)
                return
            win = SettingsWindow(self, path)
        win.present()


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else None
    # GTK lays windows out right to left in a right-to-left language (like
    # Hebrew) even when the text isn't translated; without a translation of
    # owbox Settings the text is English, so keep English's direction
    import gettext
    if gettext.find("owbox-settings") is None:
        Gtk.Widget.set_default_direction(Gtk.TextDirection.LTR)
    return SettingsApp(path).run([sys.argv[0]])
