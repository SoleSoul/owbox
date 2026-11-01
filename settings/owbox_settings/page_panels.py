# page_panels.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Panels page: the panel program, and the panels mode (the key,
the launcher, hiding and dimming)."""

import os
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gio, GLib, Gtk  # noqa: E402

from . import services  # noqa: E402
from .keybinds import key_label, launcher_choices  # noqa: E402
from .widgets import label, row, section, page_box  # noqa: E402

PANEL = "waybar"


def examples_dir(name):
    """owbox's example configuration for a program, if installed"""
    dirs = (os.environ.get("XDG_DATA_DIRS") or
            "/usr/local/share:/usr/share").split(":")
    for d in dirs:
        path = os.path.join(d, "owbox", "examples", name)
        if d and os.path.isdir(path):
            return path
    return None


class PanelsPages:
    """Panels"""

    def page_panels(self):
        scroll, box = page_box()

        box.append(section(_("Panel")))
        self.panel_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.panel_box)

        box.append(section(_("Panels on a key")))
        box.append(label(_("A key brings the panels above the dimmed "
                           "windows, with a launcher.  They go with Escape, "
                           "a click elsewhere or switching to a window."),
                         css="dim-label"))
        self.panels_key_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.panels_key_box)
        box.append(row(_("Hide the panels until then"),
                       self.switch("owbox/panels/autoHide", False),
                       _("The windows get the whole screen")))
        dims = [("0", _("No")), ("20", _("A little")), ("40", _("Some")),
                ("60", _("Much"))]
        box.append(row(_("Dim the windows meanwhile"),
                       self.dropdown("owbox/panels/dim", dims, "40")))

        self.fill_panel()
        self.fill_panels()
        return scroll

    def fill_panel(self):
        """The panel program, and whether it starts with the session"""
        box = self.panel_box
        child = box.get_first_child()
        while child:
            box.remove(child)
            child = box.get_first_child()
        svc = services.find(PANEL + ".service")
        if svc is None:
            box.append(label(_("Install waybar for a panel: a bar along an "
                               "edge of the screen with the windows, the "
                               "desktops, the time and the tray."),
                             css="dim-label"))
            return
        sw = Gtk.Switch(active=bool(svc.enabled))
        sw.set_sensitive(svc.enabled != "system")
        sw.connect("notify::active", lambda w, p: self.set_service(
            svc, w.get_active(), self.fill_panel))
        box.append(row(_("Start waybar with the session"), sw,
                       _("Turned on for all users by the system")
                       if svc.enabled == "system" else
                       _("The waybar service, also listed in Autostart")))
        examples = examples_dir(PANEL)
        conf = os.path.join(os.environ.get("XDG_CONFIG_HOME") or
                            os.path.expanduser("~/.config"), PANEL)
        if examples and not os.path.isdir(conf):
            box.append(label(_("owbox's configuration for waybar, made for "
                               "the panels on a key, is in %s.  Copy it to "
                               "%s to use it.") %
                             (examples, conf.replace(os.path.expanduser("~"),
                                                     "~", 1)),
                             css="dim-label"))

    def panels_binding(self):
        for b in self.keys.bindings():
            if b.action_name().lower() == "togglepanels":
                return b
        return None

    def fill_panels(self):
        """The key and the launcher (TogglePanels' binding)"""
        box = self.panels_key_box
        child = box.get_first_child()
        while child:
            box.remove(child)
            child = box.get_first_child()
        b = self.panels_binding()

        keys = Gtk.Box(spacing=12)
        name = label(key_label(b.key) if b else _("None"),
                     css=None if b else "dim-label")
        name.set_wrap(False)
        keys.append(name)
        change = Gtk.Button(label=_("Change…") if b else _("Choose…"))
        change.connect("clicked", lambda w: self.edit_binding(
            b, action="TogglePanels"))
        keys.append(change)
        box.append(row(_("Key"), keys,
                       _("A key alone, like Super, works when it's tapped")))

        current = b.option() if b else None
        choices = launcher_choices(current or "")
        values = [c[0] for c in choices]
        dd = Gtk.DropDown.new_from_strings([c[1] for c in choices])
        dd.set_selected(values.index(current or ""))
        dd.set_sensitive(b is not None and b.is_simple())

        def on_launcher(w, p):
            self.ensure_user_file()
            b.set_action("TogglePanels", values[w.get_selected()])
            self.changed()
            self.fill_keys()
        dd.connect("notify::selected", on_launcher)
        box.append(row(_("Launcher"), dd))

    def set_service(self, svc, on, done):
        """Turn a service on or off with systemctl, then call done()"""
        argv = services.systemctl_argv(svc, on)
        try:
            proc = Gio.Subprocess.new(argv, Gio.SubprocessFlags.STDERR_PIPE)
        except GLib.Error as e:
            self.error(_("Unable to run %s: %s") % (argv[0], e.message))
            done()
            return

        def finished(p, res):
            try:
                ok, out, err = p.communicate_utf8_finish(res)
                failed = p.get_exit_status() != 0
            except GLib.Error as e:
                failed, err = True, e.message
            if failed:
                self.error((_("Unable to turn %s on: %s") if on else
                            _("Unable to turn %s off: %s")) %
                           (svc.program, (err or "").strip()))
            self.services_changed()
            done()
        proc.communicate_utf8_async(None, None, finished)

    def services_changed(self):
        if getattr(self, "fill_services", None):
            self.fill_services()

