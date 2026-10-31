# page_autostart.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Autostart page."""

import os
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import autostart  # noqa: E402
from .widgets import label, section, page_box  # noqa: E402


class AutostartPages:
    """Autostart"""

    # ---- Autostart ----

    def page_autostart(self):
        scroll, box = page_box()
        box.append(section(_("Programs to start")))
        box.append(label(_("These run when owbox starts (from %s). "
                           "Applications in ~/.config/autostart start too.")
                         % autostart.user_path().replace(
                             os.path.expanduser("~"), "~", 1),
                         css="dim-label"))
        self.auto_list = Gtk.ListBox()
        self.auto_list.add_css_class("boxed-list")
        self.auto_list.set_selection_mode(Gtk.SelectionMode.NONE)
        self.auto_list.set_margin_top(6)
        box.append(self.auto_list)
        add_box = Gtk.Box(spacing=6)
        add_box.set_margin_top(6)
        entry = Gtk.Entry(placeholder_text=_("A command, like: waybar"))
        entry.set_hexpand(True)
        add = Gtk.Button(label=_("Add"))

        def on_add(*args):
            if entry.get_text().strip():
                autostart.add(entry.get_text().strip())
                entry.set_text("")
                self.fill_autostart()
        add.connect("clicked", on_add)
        entry.connect("activate", on_add)
        add_box.append(entry)
        add_box.append(add)
        box.append(add_box)

        if os.path.isfile(autostart.openbox_path()):
            box.append(section(_("From Openbox")))
            box.append(label(_("Your Openbox autostart has these. Programs "
                               "made for X are marked with what to use "
                               "instead."), css="dim-label"))
            ob = Gtk.ListBox()
            ob.add_css_class("boxed-list")
            ob.set_selection_mode(Gtk.SelectionMode.NONE)
            ob.set_margin_top(6)
            for cmd, note in autostart.openbox_commands():
                r = Gtk.Box(spacing=6)
                for side in ("start", "end", "top", "bottom"):
                    getattr(r, "set_margin_" + side)(6)
                texts = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
                texts.set_hexpand(True)
                texts.append(label(cmd))
                if note:
                    texts.append(label(note, css="dim-label"))
                r.append(texts)
                b = Gtk.Button(label=_("Add"))
                b.connect("clicked", lambda w, c=cmd: (autostart.add(c),
                                                       self.fill_autostart()))
                if note:
                    b.set_sensitive(False)
                r.append(b)
                ob.append(r)
            box.append(ob)
        self.fill_autostart()
        return scroll

    def fill_autostart(self):
        child = self.auto_list.get_first_child()
        while child:
            self.auto_list.remove(child)
            child = self.auto_list.get_first_child()
        cmds = autostart.commands()
        if not cmds:
            self.auto_list.append(label(_("Nothing"), css="dim-label"))
        for i, cmd in enumerate(cmds):
            r = Gtk.Box(spacing=6)
            for side in ("start", "end", "top", "bottom"):
                getattr(r, "set_margin_" + side)(6)
            t = label(cmd)
            t.set_hexpand(True)
            r.append(t)
            b = Gtk.Button(icon_name="list-remove-symbolic",
                           tooltip_text=_("Remove"))
            b.add_css_class("flat")
            b.connect("clicked", lambda w, c=cmd: (autostart.remove(c),
                                                   self.fill_autostart()))
            r.append(b)
            self.auto_list.append(r)
