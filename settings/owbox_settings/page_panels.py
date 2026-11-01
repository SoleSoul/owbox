# page_panels.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Launcher & Panels page: the launcher and panels key (the key, the
launcher, dimming), and the panels (the program, hiding them)."""

import os
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gio, GLib, Gtk  # noqa: E402

from . import autostart, panels, services  # noqa: E402
from .keybinds import key_label, launcher_choices  # noqa: E402
from .widgets import label, row, section, page_box  # noqa: E402

# the choice that asks for a command
OTHER = "\0other"


def examples_dir(name):
    """owbox's example configuration for a program, if installed"""
    dirs = (os.environ.get("XDG_DATA_DIRS") or
            "/usr/local/share:/usr/share").split(":")
    for d in dirs:
        path = os.path.join(d, "owbox", "examples", name)
        if d and os.path.isdir(path):
            return path
    return None


def clear(box):
    child = box.get_first_child()
    while child:
        box.remove(child)
        child = box.get_first_child()


def other_row(title, text, on_use):
    """A command to type, used with Enter or the button"""
    entry = Gtk.Entry(text=text or "", hexpand=True,
                      placeholder_text=_("A command"))
    use = Gtk.Button(label=_("Use"))
    box = Gtk.Box(spacing=6)
    box.append(entry)
    box.append(use)

    def used(*args):
        if entry.get_text().strip():
            on_use(entry.get_text().strip())
    entry.connect("activate", used)
    use.connect("clicked", used)
    return row(title, box)


def choice_dropdown(choices, current, on_choose, on_other):
    """A dropdown of (value, name), and Other command… at the end"""
    choices = list(choices) + [(OTHER, _("Other command…"))]
    values = [c[0] for c in choices]
    dd = Gtk.DropDown.new_from_strings([c[1] for c in choices])
    if current in values:
        dd.set_selected(values.index(current))

    def chosen(w, p):
        v = values[w.get_selected()]
        # the page is rebuilt after a change, not while in its signal
        if v == OTHER:
            GLib.idle_add(lambda: on_other() and False)
        elif v != current:
            GLib.idle_add(lambda: on_choose(v) and False)
    dd.connect("notify::selected", chosen)
    return dd


class PanelsPages:
    """Panels"""

    def page_panels(self):
        scroll, box = page_box()

        box.append(section(_("The launcher and panels key")))
        box.append(label(_("Tap a key to open a launcher, with the panels "
                           "above the dimmed windows.  Escape, a click "
                           "elsewhere or picking a window puts them away."),
                         css="dim-label"))
        self.panels_key_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.panels_key_box)

        box.append(section(_("Panels")))
        box.append(label(_("Bars along an edge of the screen, with the "
                           "windows, the desktops, the time and the tray."),
                         css="dim-label"))
        self.panel_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.panel_box)

        self.fill_panels()
        self.fill_panel()
        return scroll

    # ---- the panel program ----

    def fill_panel(self, other=False):
        """other: show the command to type"""
        box = self.panel_box
        clear(box)
        cmd, svc = panels.current()
        choices = [("", _("None"))] + panels.installed()
        if cmd and cmd not in [c[0] for c in choices]:
            choices.append((cmd, cmd))
        if other:
            hint = _("Started when you log in, from Autostart")
        elif svc is not None:
            hint = _("Started when you log in, by its service (%s)") % \
                svc.name
        elif cmd:
            hint = _("Started when you log in, from Autostart.  A change "
                     "applies at the next login.")
        else:
            hint = _("None is started when you log in")
        dd = choice_dropdown(choices, OTHER if other else (cmd or ""),
                             self.set_panel, lambda: self.fill_panel(True))
        box.append(row(_("Panel program"), dd, hint))
        if other:
            box.append(other_row(_("Command"), "", self.set_panel))
        if len(choices) == 1 and not other:
            box.append(label(_("Install waybar for one."), css="dim-label"))
        examples = examples_dir("waybar")
        conf = os.path.join(os.environ.get("XDG_CONFIG_HOME") or
                            os.path.expanduser("~/.config"), "waybar")
        if cmd == "waybar" and examples and not os.path.isdir(conf):
            box.append(label(_("owbox's configuration for waybar, made for "
                               "the panels key, is in %s.  Copy it to %s to "
                               "use it.") %
                             (examples, conf.replace(os.path.expanduser("~"),
                                                     "~", 1)),
                             css="dim-label"))

        hide = self.switch("owbox/panels/autoHide", False)
        b = self.panels_binding()
        hide.set_sensitive(b is not None)
        box.append(row(_("Hide the panels the rest of the time"), hide,
                       _("Only the key shows them, and the windows get the "
                         "whole screen") if b else
                       _("Choose a key first, it's what shows them")))

    def set_panel(self, cmd):
        """Start cmd with the session instead of the panel started now
        ("" for none)"""
        old, old_svc = panels.current()
        if cmd == (old or ""):
            self.fill_panel()
            return
        new_svc = panels.service_of(cmd)

        def refresh():
            self.fill_panel()
            self.fill_autostart()

        def start_new():
            try:
                autostart.set_panel(None if new_svc else cmd)
            except OSError as e:
                self.error(_("Unable to save %s: %s") %
                           (autostart.user_path(), e))
            if new_svc is not None:
                self.set_service(new_svc, True, refresh)
            else:
                refresh()
        if old_svc is not None:
            self.set_service(old_svc, False, start_new)
        else:
            start_new()

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
            self.fill_services()
            done()
        proc.communicate_utf8_async(None, None, finished)

    # ---- the panels key ----

    def panels_binding(self):
        for b in self.keys.bindings():
            if b.action_name().lower() == "togglepanels":
                return b
        return None

    def fill_panels(self, other=False):
        """The key, the launcher (TogglePanels' binding) and dimming.
        other: show the launcher command to type"""
        box = self.panels_key_box
        clear(box)
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
                       _("A key alone, like Super, or two together, like "
                         "Ctrl+Super")))

        current = (b.option() or "") if b else ""
        dd = choice_dropdown(launcher_choices(current),
                             OTHER if other else current, self.set_launcher,
                             lambda: self.fill_panels(True))
        dd.set_sensitive(b is not None and b.is_simple())
        box.append(row(_("Launcher"), dd))
        if other:
            box.append(other_row(_("Command"), current, self.set_launcher))

        dims = [("0", _("No")), ("20", _("A little")), ("40", _("Some")),
                ("60", _("Much"))]
        box.append(row(_("Dim the windows"),
                       self.dropdown("owbox/panels/dim", dims, "40")))

    def set_launcher(self, cmd):
        b = self.panels_binding()
        if b is None:
            return
        self.ensure_user_file()
        b.set_action("TogglePanels", cmd)
        self.changed()
        self.bindings_changed()
