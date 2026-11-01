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
from gi.repository import GLib, Gtk  # noqa: E402

from . import autostart, services  # noqa: E402
from .widgets import label, section, page_box  # noqa: E402


class AutostartPages:
    """Autostart"""

    # ---- Autostart ----

    def page_autostart(self):
        scroll, box = page_box()
        box.append(section(_("Programs to start")))
        box.append(label(_("These run when owbox starts (from %s).")
                         % autostart.user_path().replace(
                             os.path.expanduser("~"), "~", 1),
                         css="dim-label"))
        self.auto_list = self.boxed_list()
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

        box.append(section(_("Applications")))
        box.append(label(_("Applications set to start when you log in "
                           "(in ~/.config/autostart, or for all users)."),
                         css="dim-label"))
        self.xdg_list = self.boxed_list()
        box.append(self.xdg_list)

        box.append(section(_("Services")))
        box.append(label(_("Programs that start with the session and stop "
                           "when it ends, like a panel (systemd user "
                           "services)."), css="dim-label"))
        self.services_list = self.boxed_list()
        box.append(self.services_list)

        more = label(_("owbox also starts an authentication agent (for "
                       "administrator passwords) if one is installed.  "
                       "Notifications, file choosers and the launcher start "
                       "when they are needed."), css="dim-label")
        more.set_margin_top(12)
        box.append(more)

        if os.path.isfile(autostart.openbox_path()):
            box.append(section(_("From Openbox")))
            box.append(label(_("Your Openbox autostart has these. Programs "
                               "made for X are marked with what to use "
                               "instead."), css="dim-label"))
            ob = self.boxed_list()
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
        self.fill_xdg()
        self.fill_services()
        return scroll

    @staticmethod
    def boxed_list():
        lb = Gtk.ListBox()
        lb.add_css_class("boxed-list")
        lb.set_selection_mode(Gtk.SelectionMode.NONE)
        lb.set_margin_top(6)
        return lb

    @staticmethod
    def switch_row(title, subtitle, active, on_toggled, sensitive=True):
        r = Gtk.Box(spacing=6)
        for side in ("start", "end", "top", "bottom"):
            getattr(r, "set_margin_" + side)(6)
        texts = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        texts.set_hexpand(True)
        texts.append(label(title))
        if subtitle:
            texts.append(label(subtitle, css="dim-label"))
        r.append(texts)
        sw = Gtk.Switch(active=active, valign=Gtk.Align.CENTER)
        sw.set_sensitive(sensitive)
        sw.connect("notify::active", lambda w, p: on_toggled(w.get_active()))
        r.append(sw)
        return r

    @staticmethod
    def clear(lb):
        child = lb.get_first_child()
        while child:
            lb.remove(child)
            child = lb.get_first_child()

    def fill_xdg(self):
        self.clear(self.xdg_list)
        entries = autostart.xdg_entries()
        if not entries:
            self.xdg_list.append(label(_("None"), css="dim-label"))
        for e in entries:
            self.xdg_list.append(self.switch_row(
                e.name, e.filename, e.on,
                lambda on, e=e: self.set_xdg_on(e, on)))

    def set_xdg_on(self, entry, on):
        try:
            autostart.set_xdg_on(entry, on)
        except (OSError, GLib.Error) as e:
            self.error(_("Unable to change %s: %s") % (entry.filename, e))
            self.fill_xdg()

    def fill_services(self):
        self.clear(self.services_list)
        svcs = services.session_services()
        if not svcs:
            self.services_list.append(label(_("None"), css="dim-label"))
        for s in svcs:
            sub = s.description or s.name
            if s.enabled == "system":
                sub += "\n" + _("Turned on for all users by the system")
            self.services_list.append(self.switch_row(
                s.program, sub, bool(s.enabled),
                lambda on, s=s: self.set_service(s, on, self.fill_panel),
                sensitive=s.enabled != "system"))

    def fill_autostart(self):
        self.clear(self.auto_list)
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
