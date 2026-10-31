# page_windows.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Windows, Displays and Desktops pages."""

import shutil
import subprocess
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from .rcxml import q  # noqa: E402
from .widgets import label, row, section, page_box  # noqa: E402


class WindowsPages:
    """Windows, Displays and Desktops"""

    # ---- Windows ----

    def page_windows(self):
        scroll, box = page_box()
        box.append(section(_("Focus")))
        focus = Gtk.DropDown.new_from_strings([_("Click to focus"),
                                               _("Focus follows the mouse")])
        focus.set_selected(1 if self.rc.get_bool("focus/followMouse") else 0)
        box.append(row(_("Focus windows by"), focus))
        raise_ = self.switch("focus/raiseOnFocus", False)
        delay = self.spin("focus/focusDelay", 0, 2000, 200, 50)
        raise_row = row(_("Raise windows when they get focus"), raise_)
        delay_row = row(_("Delay before focusing (ms)"), delay)
        box.append(raise_row)
        box.append(delay_row)

        def on_focus(w, p):
            follow = w.get_selected() == 1
            self.set_bool("focus/followMouse", follow)
            raise_row.set_sensitive(follow)
            delay_row.set_sensitive(follow)
        focus.connect("notify::selected", on_focus)
        raise_row.set_sensitive(focus.get_selected() == 1)
        delay_row.set_sensitive(focus.get_selected() == 1)
        box.append(row(_("Focus new windows"),
                       self.switch("focus/focusNew", True)))

        box.append(section(_("Placement")))
        # Openbox's <placement><policy>, and how owbox does its 'Smart'
        place = Gtk.DropDown.new_from_strings([_("In the middle"),
                                               _("Where there is room"),
                                               _("Under the mouse")])
        if (self.rc.get("placement/policy") or "").lower() == "undermouse":
            place.set_selected(2)
        elif (self.rc.get("owbox/placement") or "").lower() == "openbox":
            place.set_selected(1)
        else:
            place.set_selected(0)
        box.append(row(_("Place new windows"), place))
        center_row = row(_("Center in the free area"),
                         self.switch("placement/center", True))
        box.append(center_row)

        def on_place(w, p):
            i = w.get_selected()
            self.set("placement/policy", "UnderMouse" if i == 2 else "Smart")
            if i != 2:
                self.set("owbox/placement", "openbox" if i == 1 else
                         "cascade")
            center_row.set_sensitive(i == 1)
        place.connect("notify::selected", on_place)
        center_row.set_sensitive(place.get_selected() == 1)
        box.append(row(_("Open new windows on"), self.dropdown(
            "placement/monitor",
            [("Current", _("The monitor in use")),
             ("Mouse", _("The monitor with the mouse")),
             ("Active", _("The focused window's monitor")),
             ("Primary", _("The primary monitor")),
             ("Any", _("Any monitor"))],
            "Current")))

        box.append(section(_("Snapping")))
        snap = Gtk.Switch(active=(self.rc.find("owbox/snapping") is None or
                                  self.rc.find("owbox/snapping").get(
                                      "enabled", "yes").lower() in
                                  ("yes", "true", "on")))

        def on_snap(w, p):
            self.ensure_user_file()
            e = self.rc.ensure("owbox/snapping")
            e.set("enabled", "yes" if w.get_active() else "no")
            en = self.rc.find("owbox/snapping/enabled")
            if en is not None:
                en.text = "yes" if w.get_active() else "no"
            self.changed()
        snap.connect("notify::active", on_snap)
        box.append(row(_("Snap windows dropped at the screen's edges"), snap,
                       _("The sides fill half the screen, the corners a "
                         "quarter")))
        box.append(row(_("Dropping at the top maximizes"),
                       self.switch("owbox/snapping/topMaximize", True)))
        box.append(row(_("Show where the window will go"),
                       self.switch("owbox/snapping/preview", True)))

        box.append(section(_("Decorations")))
        box.append(row(_("Window frames"), self.dropdown(
            "owbox/decorations",
            [("auto", _("Unless the application draws its own")),
             ("server", _("On all windows that allow it")),
             ("client", _("Let applications draw their own"))], "auto"),
            _("Applications like GTK 4 ones draw their own title bar")))

        box.append(section(_("Moving and resizing")))
        box.append(row(_("Draw window contents while resizing"),
                       self.switch("resize/drawContents", True)))
        box.append(row(_("Resistance at the screen's edges (pixels)"),
                       self.spin("resistance/screen_edge_strength", 0, 100,
                                 20)))
        box.append(row(_("Resistance between windows (pixels)"),
                       self.spin("resistance/strength", 0, 100, 10)))
        return scroll

    # ---- Displays ----

    def page_displays(self):
        scroll, box = page_box()
        outputs = owbox_outputs()
        if not outputs:
            box.append(label(_("owbox isn't running, the monitors can be "
                               "set while it is."), css="dim-label"))
            self.idle_section(box)
            return scroll
        for o in outputs:
            name = o["name"]
            title = " ".join(x for x in (o.get("make"), o.get("model")) if x)
            box.append(section("%s%s" % (name, " — " + title if title
                                         else "")))
            base = "owbox/outputs/output[name=%s]/" % name

            box.append(row(_("On"), self.switch(base + "enabled",
                                                o["enabled"])))
            modes, seen = [], set()
            for m in o.get("modes", []):
                value = "%dx%d@%.3f" % (m["width"], m["height"],
                                        m["refresh"] / 1000)
                if value in seen:
                    continue
                seen.add(value)
                text = "%d × %d, %.2f Hz" % (m["width"], m["height"],
                                             m["refresh"] / 1000)
                if m.get("preferred"):
                    text += " " + _("(recommended)")
                modes.append((value, text))
            if modes:
                cur = "%dx%d@%.3f" % (o["width"], o["height"],
                                      o["refresh"] / 1000)
                configured = self.rc.get(base + "mode")
                dd = Gtk.DropDown.new_from_strings([m[1] for m in modes])
                values = [m[0] for m in modes]
                pick = configured if configured in values else cur
                if pick in values:
                    dd.set_selected(values.index(pick))
                dd.connect("notify::selected", lambda w, p, b=base, v=values:
                           self.set(b + "mode", v[w.get_selected()]))
                box.append(row(_("Resolution"), dd))
            scales = [("1", "100%"), ("1.25", "125%"), ("1.5", "150%"),
                      ("1.75", "175%"), ("2", "200%"), ("2.5", "250%"),
                      ("3", "300%")]
            cur_scale = "%g" % o.get("scale", 1)
            if self.rc.get(base + "scale") is None and \
                    cur_scale in [s[0] for s in scales]:
                pass
            dd = Gtk.DropDown.new_from_strings([s[1] for s in scales])
            svalues = [s[0] for s in scales]
            sc = self.rc.get(base + "scale", cur_scale)
            if sc in svalues:
                dd.set_selected(svalues.index(sc))
            dd.connect("notify::selected", lambda w, p, b=base, v=svalues:
                       self.set(b + "scale", v[w.get_selected()]))
            box.append(row(_("Scale"), dd, _("Bigger text and pictures on "
                                             "high resolution screens")))
            rots = [("normal", _("None")), ("90", _("90°")),
                    ("180", _("180°")), ("270", _("270°"))]
            dd = Gtk.DropDown.new_from_strings([r[1] for r in rots])
            rvalues = [r[0] for r in rots]
            tr = self.rc.get(base + "transform", o.get("transform", "normal"))
            if tr in rvalues:
                dd.set_selected(rvalues.index(tr))
            dd.connect("notify::selected", lambda w, p, b=base, v=rvalues:
                       self.set(b + "transform", v[w.get_selected()]))
            box.append(row(_("Rotation"), dd))

            pos = Gtk.Box(spacing=6)
            xs = Gtk.SpinButton.new_with_range(-32768, 32768, 1)
            ys = Gtk.SpinButton.new_with_range(-32768, 32768, 1)
            xs.set_value(o.get("x", 0))
            ys.set_value(o.get("y", 0))

            def on_pos(w, b=base, xs=xs, ys=ys):
                self.ensure_user_file()
                e = self.rc.ensure(b + "position")
                e.set("x", str(int(xs.get_value())))
                e.set("y", str(int(ys.get_value())))
                self.changed()
            xs.connect("value-changed", on_pos)
            ys.connect("value-changed", on_pos)
            pos.append(xs)
            pos.append(label("×"))
            pos.append(ys)
            box.append(row(_("Position"), pos,
                           _("Where its top left corner is among the "
                             "monitors")))
        self.idle_section(box)
        return scroll

    def idle_section(self, box):
        """<owbox><idle>: the monitors off when idle, and locking"""
        box.append(section(_("When idle")))
        minutes = ["0", "1", "2", "3", "5", "10", "15", "20", "30", "45",
                   "60"]
        names = [_("Never")] + [_("After %s minutes") % m if m != "1" else
                                _("After 1 minute") for m in minutes[1:]]
        cur = self.rc.get("owbox/idle/screenOff", "10")
        if cur not in minutes:
            # set in rc.xml by hand
            minutes.append(cur)
            names.append(_("After %s minutes") % cur)
        dd = Gtk.DropDown.new_from_strings(names)
        dd.set_selected(minutes.index(cur))
        dd.connect("notify::selected", lambda w, p: self.set(
            "owbox/idle/screenOff", minutes[w.get_selected()]))
        box.append(row(_("Turn the monitors off"), dd,
                       _("When there's no input, unless a window asks to "
                         "keep them on (a video)")))
        locker = next((n for n in ("swaylock", "gtklock", "waylock",
                                   "hyprlock") if shutil.which(n)), None)
        note = _("When the monitors turn off, before suspending, and with "
                 "Lock Screen in the menu.")
        if self.rc.get("owbox/idle/lockCommand"):
            note += " " + _("Locks with %s.") % \
                self.rc.get("owbox/idle/lockCommand")
        elif locker:
            note += " " + _("Locks with %s.") % locker
        else:
            note += " " + _("Install a screen locker, like swaylock, to "
                            "lock the screen.")
        box.append(row(_("Lock the screen"),
                       self.switch("owbox/idle/lock", True), note))

    # ---- Desktops ----

    def page_desktops(self):
        scroll, box = page_box()
        box.append(section(_("Desktops")))
        number = Gtk.SpinButton.new_with_range(1, 32, 1)
        number.set_value(self.rc.get_int("desktops/number", 4))
        box.append(row(_("Number of desktops"), number))
        box.append(section(_("Names")))
        names_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.append(names_box)

        def names():
            e = self.rc.find("desktops/names")
            return [n.text or "" for n in e if n.tag == q("name")] \
                if e is not None else []

        def write_names(entries):
            self.ensure_user_file()
            e = self.rc.ensure("desktops/names")
            for n in [n for n in e if n.tag == q("name")]:
                self.rc.remove(e, n)
            for ent in entries:
                self.rc.append(e, "name", text=ent.get_text())
            self.changed()

        def build(*args):
            child = names_box.get_first_child()
            while child:
                names_box.remove(child)
                child = names_box.get_first_child()
            current = names()
            entries = []
            for i in range(int(number.get_value())):
                ent = Gtk.Entry(text=current[i] if i < len(current) else
                                _("desktop %d") % (i + 1))
                entries.append(ent)
                names_box.append(row(_("Desktop %d") % (i + 1), ent))
            for ent in entries:
                ent.connect("changed", lambda w: write_names(entries))

        def on_number(w):
            self.set("desktops/number", str(int(w.get_value())))
            build()
        number.connect("value-changed", on_number)
        build()
        return scroll


def owbox_outputs():
    """The monitors, from the running owbox"""
    import json
    try:
        r = subprocess.run(["owbox", "--msg", "outputs"], capture_output=True,
                           text=True, timeout=3)
        return [json.loads(l) for l in r.stdout.splitlines() if l.strip()]
    except (OSError, subprocess.SubprocessError, ValueError):
        return []
