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
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Pango", "1.0")
from gi.repository import Gdk, Gio, GLib, Gtk, Pango  # noqa: E402

from .rcxml import (RcXml, find_config, running_config, owbox_config_path,  # noqa: E402
                    reconfigure, config_home, q)
from .keybinds import (KeyBindings, ACTIONS, ACTION_INFO, key_label,  # noqa: E402
                       key_from_event)
from . import xkb  # noqa: E402
from . import autostart  # noqa: E402

APP_ID = "org.owbox.Settings"
SAVE_DELAY = 400  # ms


def label(text, xalign=0.0, css=None):
    lab = Gtk.Label(label=text, xalign=xalign)
    lab.set_wrap(True)
    if css:
        lab.add_css_class(css)
    return lab


def row(title, widget, subtitle=None):
    """A settings row: the title (and a hint) on the left, the control on
    the right"""
    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
    box.set_margin_top(6)
    box.set_margin_bottom(6)
    texts = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    texts.set_hexpand(True)
    texts.append(label(title))
    if subtitle:
        texts.append(label(subtitle, css="dim-label"))
    box.append(texts)
    widget.set_valign(Gtk.Align.CENTER)
    box.append(widget)
    return box


def section(title):
    lab = label(title, css="heading")
    lab.set_margin_top(18)
    lab.set_margin_bottom(6)
    return lab


def page_box():
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
    for side in ("start", "end"):
        getattr(box, "set_margin_" + side)(24)
    box.set_margin_bottom(24)
    box.set_margin_top(6)
    scroll = Gtk.ScrolledWindow()
    scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
    scroll.set_child(box)
    scroll.set_vexpand(True)
    return scroll, box


class SettingsWindow(Gtk.ApplicationWindow):
    def __init__(self, app, path):
        super().__init__(application=app, title=_("owbox Settings"))
        self.set_default_size(820, 620)
        self.rc = RcXml(path)
        self.keys = KeyBindings(self.rc)
        self.save_id = 0

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

        pages = [("appearance", _("Appearance"), self.page_appearance),
                 ("windows", _("Windows"), self.page_windows),
                 ("displays", _("Displays"), self.page_displays),
                 ("desktops", _("Desktops"), self.page_desktops),
                 ("keyboard", _("Keyboard"), self.page_keyboard),
                 ("mouse", _("Mouse & Touchpad"), self.page_mouse),
                 ("keys", _("Key Bindings"), self.page_keys),
                 ("autostart", _("Autostart"), self.page_autostart)]
        for name, title, build in pages:
            self.stack.add_titled(build(), name, title)

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

    def set_or_unset(self, path, value):
        if value == "":
            self.ensure_user_file()
            self.rc.unset(path)
            self.changed()
        else:
            self.set(path, value)

    # ---- Appearance ----

    def page_appearance(self):
        scroll, box = page_box()
        box.append(section(_("Theme")))
        flow = Gtk.FlowBox()
        flow.set_selection_mode(Gtk.SelectionMode.SINGLE)
        flow.set_min_children_per_line(2)
        flow.set_max_children_per_line(3)
        flow.set_homogeneous(True)
        current = self.rc.get("theme/name", "Clearlooks")
        self.theme_children = {}
        for name in themes():
            item = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            pic = Gtk.Picture()
            pic.set_size_request(220, 112)
            pic.set_content_fit(Gtk.ContentFit.SCALE_DOWN)
            item.append(pic)
            item.append(label(name, xalign=0.5))
            flow.append(item)
            child = item.get_parent()
            child.theme_name = name
            self.theme_children[name] = child
            GLib.idle_add(self.load_preview, name, pic)
            if name == current:
                flow.select_child(child)
        flow.connect("child-activated",
                     lambda f, c: self.set("theme/name", c.theme_name))
        flow.connect("selected-children-changed", self.on_theme_selected)
        box.append(flow)

        box.append(section(_("Title bar")))
        layout = Gtk.Entry(text=self.rc.get("theme/titleLayout", "NLIMC"))
        layout.connect("changed", lambda w: self.set("theme/titleLayout",
                                                     w.get_text().upper()))
        box.append(row(_("Buttons"), layout,
                       _("N icon, L title, I iconify, M maximize, C close, "
                         "S roll up, D all desktops, in their order")))

        box.append(section(_("Desktop")))
        box.append(row(_("Icon theme"), self.dropdown(
            "owbox/icons/theme", [("", _("The one GTK applications use"))] +
            [(t, t) for t in icon_themes()], ""),
            _("For window and menu icons")))
        box.append(row(_("Cursor theme"), self.dropdown(
            "owbox/cursor/theme", [("", _("The one GTK applications use"))] +
            [(t, t) for t in cursor_themes()], "")))
        box.append(row(_("Cursor size"),
                       self.spin("owbox/cursor/size", 16, 96, 24, 8)))
        color = Gtk.ColorDialogButton(dialog=Gtk.ColorDialog())
        rgba = Gdk.RGBA()
        if not rgba.parse(self.rc.get("owbox/background", "#303030")):
            rgba.parse("#303030")
        color.set_rgba(rgba)
        color.connect("notify::rgba", lambda w, p: self.set(
            "owbox/background", "#%02x%02x%02x" % (
                int(w.get_rgba().red * 255), int(w.get_rgba().green * 255),
                int(w.get_rgba().blue * 255))))
        box.append(row(_("Background color"), color,
                       _("Shown where no wallpaper program (like swaybg) "
                         "draws")))

        box.append(section(_("Fonts")))
        box.append(row(_("Window titles"),
                       self.font_button(("ActiveWindow", "InactiveWindow"))))
        box.append(row(_("Menus"), self.font_button(("MenuItem",))))
        box.append(row(_("Menu titles"), self.font_button(("MenuHeader",))))
        box.append(row(_("Popups (Alt-Tab, desktop names)"),
                       self.font_button(("ActiveOnScreenDisplay",
                                         "InactiveOnScreenDisplay"))))
        return scroll

    def on_theme_selected(self, flow):
        sel = flow.get_selected_children()
        if sel and sel[0].theme_name != self.rc.get("theme/name"):
            self.set("theme/name", sel[0].theme_name)

    def load_preview(self, name, pic):
        path = theme_preview(name)
        if path:
            pic.set_filename(path)
        return False

    def font_button(self, places):
        base = "theme/font[place=%s]/" % places[0]
        desc = Pango.FontDescription.from_string(
            "%s %s" % (self.rc.get(base + "name", "sans"),
                       self.rc.get(base + "size", "8")))
        if (self.rc.get(base + "weight") or "").lower() == "bold":
            desc.set_weight(Pango.Weight.BOLD)
        if (self.rc.get(base + "slant") or "").lower() == "italic":
            desc.set_style(Pango.Style.ITALIC)
        button = Gtk.FontDialogButton(dialog=Gtk.FontDialog())
        button.set_font_desc(desc)
        button.set_level(Gtk.FontLevel.FONT)

        def on_font(w, p):
            d = w.get_font_desc()
            if d is None:
                return
            for place in places:
                b = "theme/font[place=%s]/" % place
                self.ensure_user_file()
                self.rc.set(b + "name", d.get_family() or "sans")
                self.rc.set(b + "size", str(max(1, d.get_size() //
                                                Pango.SCALE)))
                self.rc.set(b + "weight", "bold" if d.get_weight() >=
                            Pango.Weight.BOLD else "normal")
                self.rc.set(b + "slant", "italic" if d.get_style() !=
                            Pango.Style.NORMAL else "normal")
            self.changed()
        button.connect("notify::font-desc", on_font)
        return button

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
        box.append(row(_("Place new windows"), self.dropdown(
            "placement/policy", [("Smart", _("Where there is room")),
                                 ("UnderMouse", _("Under the mouse"))],
            "Smart")))
        box.append(row(_("Center new windows"),
                       self.switch("placement/center", True)))

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
        return scroll

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

    # ---- Keyboard ----

    def page_keyboard(self):
        scroll, box = page_box()
        registry = xkb.Registry()
        box.append(section(_("Layouts")))
        box.append(label(_("The first layout is the default, the others are "
                           "switched to with the key below."),
                         css="dim-label"))
        lst = Gtk.ListBox()
        lst.set_selection_mode(Gtk.SelectionMode.NONE)
        lst.add_css_class("boxed-list")
        lst.set_margin_top(6)
        box.append(lst)

        def current():
            layouts = [s.strip() for s in
                       (self.rc.get("owbox/keyboard/layout") or "").split(",")
                       if s.strip()]
            variants = (self.rc.get("owbox/keyboard/variant") or "").split(",")
            variants += [""] * (len(layouts) - len(variants))
            return list(zip(layouts, variants))

        def write(pairs):
            self.ensure_user_file()
            if pairs:
                self.rc.set("owbox/keyboard/layout",
                            ",".join(p[0] for p in pairs))
                if any(p[1] for p in pairs):
                    self.rc.set("owbox/keyboard/variant",
                                ",".join(p[1] for p in pairs))
                else:
                    self.rc.unset("owbox/keyboard/variant")
            else:
                self.rc.unset("owbox/keyboard/layout")
                self.rc.unset("owbox/keyboard/variant")
            self.changed()
            fill()

        def fill():
            child = lst.get_first_child()
            while child:
                lst.remove(child)
                child = lst.get_first_child()
            pairs = current()
            if not pairs:
                lst.append(label(_("The system's default layout"), css="dim-label"))
            for i, (lay, var) in enumerate(pairs):
                r = Gtk.Box(spacing=6)
                for side in ("start", "end", "top", "bottom"):
                    getattr(r, "set_margin_" + side)(6)
                name = label(registry.describe(lay, var))
                name.set_hexpand(True)
                r.append(name)
                for icon, tip, func in (
                        ("go-up-symbolic", _("Move up"), "up"),
                        ("go-down-symbolic", _("Move down"), "down"),
                        ("list-remove-symbolic", _("Remove"), "remove")):
                    b = Gtk.Button(icon_name=icon, tooltip_text=tip)
                    b.add_css_class("flat")
                    b.connect("clicked", self.on_layout_button, func, i,
                              current, write)
                    r.append(b)
                lst.append(r)

        add = Gtk.Button(label=_("Add a layout…"))
        add.set_halign(Gtk.Align.START)
        add.set_margin_top(6)
        add.connect("clicked", lambda b: self.choose_layout(
            registry, lambda lay, var: write(current() + [(lay, var)])))
        box.append(add)
        fill()

        box.append(section(_("Switching layouts")))
        options = [o.strip() for o in
                   (self.rc.get("owbox/keyboard/options") or "").split(",")
                   if o.strip()]
        grp = [o for o in options if o.startswith("grp:")]
        choices = [("", _("No key"))] + registry.switch_options()
        dd = Gtk.DropDown.new_from_strings([c[1] for c in choices])
        values = [c[0] for c in choices]
        if grp and grp[0] in values:
            dd.set_selected(values.index(grp[0]))

        def on_switch(w, p):
            opts = [o.strip() for o in
                    (self.rc.get("owbox/keyboard/options") or "").split(",")
                    if o.strip() and not o.strip().startswith("grp:")]
            v = values[w.get_selected()]
            if v:
                opts.append(v)
            self.ensure_user_file()
            if opts:
                self.rc.set("owbox/keyboard/options", ",".join(opts))
            else:
                self.rc.unset("owbox/keyboard/options")
            self.changed()
        dd.connect("notify::selected", on_switch)
        box.append(row(_("Switch layouts with"), dd))

        box.append(section(_("Typing")))
        box.append(row(_("Delay before a held key repeats (ms)"),
                       self.spin("owbox/keyboard/repeatDelay", 100, 2000, 600,
                                 25)))
        box.append(row(_("Repeats per second"),
                       self.spin("owbox/keyboard/repeatRate", 1, 100, 25)))
        box.append(row(_("Num Lock on at login"),
                       self.switch("owbox/keyboard/numlock", False)))
        return scroll

    def on_layout_button(self, button, func, i, current, write):
        pairs = current()
        if func == "remove":
            del pairs[i]
        elif func == "up" and i > 0:
            pairs[i - 1], pairs[i] = pairs[i], pairs[i - 1]
        elif func == "down" and i < len(pairs) - 1:
            pairs[i + 1], pairs[i] = pairs[i], pairs[i + 1]
        write(pairs)

    def choose_layout(self, registry, done):
        win = Gtk.Window(title=_("Add a layout"), transient_for=self,
                         modal=True, default_width=420, default_height=480)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(12)
        win.set_child(box)
        search = Gtk.SearchEntry()
        box.append(search)
        lst = Gtk.ListBox()
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(lst)
        box.append(scroll)
        for lay, var, desc in registry.all_layouts():
            r = Gtk.ListBoxRow()
            r.set_child(label(desc))
            r.layout = (lay, var, desc.lower())
            lst.append(r)
        lst.set_filter_func(lambda r: search.get_text().lower() in r.layout[2])
        search.connect("search-changed", lambda w: lst.invalidate_filter())

        def chosen(lb, r):
            done(r.layout[0], r.layout[1])
            win.close()
        lst.connect("row-activated", chosen)
        win.present()

    # ---- Mouse & touchpad ----

    def libinput_path(self, match, setting):
        return "owbox/libinput/device[match=%s]/%s" % (match, setting)

    def accel(self, match):
        path = self.libinput_path(match, "accelSpeed")
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, -1, 1,
                                         0.1)
        scale.set_size_request(200, -1)
        try:
            scale.set_value(float(self.rc.get(path, "0")))
        except ValueError:
            pass
        scale.add_mark(0, Gtk.PositionType.BOTTOM, None)
        scale.connect("value-changed",
                      lambda w: self.set(path, "%.1f" % w.get_value()))
        return scale

    def page_mouse(self):
        scroll, box = page_box()
        box.append(section(_("Mouse")))
        box.append(row(_("Speed"), self.accel("pointer")))
        box.append(row(_("Natural scrolling"),
                       self.switch(self.libinput_path("pointer",
                                                      "naturalScroll"), False),
                       _("The content moves with the wheel, like on a phone")))
        box.append(row(_("Left handed"),
                       self.switch(self.libinput_path("pointer", "leftHanded"),
                                   False)))
        box.append(row(_("Double click time (ms)"),
                       self.spin("mouse/doubleClickTime", 100, 1500, 500, 50)))

        box.append(section(_("Touchpad")))
        box.append(row(_("Tap to click"),
                       self.switch(self.libinput_path("touchpad", "tap"), True)))
        box.append(row(_("Natural scrolling"),
                       self.switch(self.libinput_path("touchpad",
                                                      "naturalScroll"), True)))
        box.append(row(_("Disable while typing"),
                       self.switch(self.libinput_path("touchpad",
                                                      "disableWhileTyping"),
                                   True)))
        box.append(row(_("Speed"), self.accel("touchpad")))
        return scroll

    # ---- Key bindings ----

    def page_keys(self):
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "bottom"):
            getattr(outer, "set_margin_" + side)(24)
        outer.set_margin_top(12)
        outer.append(label(_("Double click a binding to change it."),
                           css="dim-label"))
        self.keys_list = Gtk.ListBox()
        self.keys_list.add_css_class("boxed-list")
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(self.keys_list)
        outer.append(scroll)
        self.keys_list.connect("row-activated",
                               lambda lb, r: self.edit_binding(r.binding))
        buttons = Gtk.Box(spacing=6)
        add = Gtk.Button(label=_("Add…"))
        add.connect("clicked", lambda b: self.edit_binding(None))
        edit = Gtk.Button(label=_("Change…"))
        edit.connect("clicked", lambda b: self.selected_binding() and
                     self.edit_binding(self.selected_binding()))
        remove = Gtk.Button(label=_("Remove"))
        remove.connect("clicked", self.on_remove_binding)
        for b in (add, edit, remove):
            buttons.append(b)
        outer.append(buttons)
        self.fill_keys()
        return outer

    def fill_keys(self):
        child = self.keys_list.get_first_child()
        while child:
            self.keys_list.remove(child)
            child = self.keys_list.get_first_child()
        for b in self.keys.bindings():
            r = Gtk.ListBoxRow()
            box = Gtk.Box(spacing=12)
            for side in ("start", "end", "top", "bottom"):
                getattr(box, "set_margin_" + side)(6)
            k = label(key_label(b.key))
            k.set_size_request(180, -1)
            box.append(k)
            d = label(b.description(),
                      css=None if b.is_simple() else "dim-label")
            d.set_hexpand(True)
            box.append(d)
            r.set_child(box)
            r.binding = b
            self.keys_list.append(r)

    def selected_binding(self):
        r = self.keys_list.get_selected_row()
        return r.binding if r else None

    def on_remove_binding(self, *args):
        b = self.selected_binding()
        if b:
            self.ensure_user_file()
            self.keys.remove(b)
            self.changed()
            self.fill_keys()

    def edit_binding(self, binding):
        if binding is not None and not binding.is_simple():
            self.error(_("This binding does more than the settings can show. "
                         "You can change it in %s, or remove it here.")
                       % self.rc.path)
            return
        BindingDialog(self, binding).present()

    def save_binding(self, binding, key, action, option):
        self.ensure_user_file()
        other = self.keys.find(key)
        if other is not None and (binding is None or
                                  other.elem is not binding.elem):
            self.keys.remove(other)
        if binding is None:
            self.keys.add(key, action, option)
        else:
            binding.key = key
            binding.set_action(action, option)
        self.changed()
        self.fill_keys()

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


class BindingDialog(Gtk.Window):
    """Choose the keys and the action of a binding"""

    def __init__(self, win, binding):
        super().__init__(title=_("Key Binding"), transient_for=win,
                         modal=True, default_width=460)
        self.win = win
        self.binding = binding
        self.key = binding.key if binding else ""
        self.recording = False

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(18)
        self.set_child(box)

        self.key_button = Gtk.Button(label=key_label(self.key) if self.key
                                     else _("Press to choose the keys"))
        self.key_button.connect("clicked", self.start_recording)
        box.append(row(_("Keys"), self.key_button))

        self.action = Gtk.DropDown.new_from_strings([a[1] for a in ACTIONS])
        box.append(row(_("Action"), self.action))
        self.option_box = Gtk.Box()
        box.append(self.option_box)
        if binding:
            names = [a[0].lower() for a in ACTIONS]
            self.action.set_selected(names.index(binding.action_name().lower()))
        self.action.connect("notify::selected", lambda w, p:
                            self.show_option(None))
        self.show_option(binding.option() if binding else None)

        self.warning = label("", css="dim-label")
        box.append(self.warning)
        buttons = Gtk.Box(spacing=6, halign=Gtk.Align.END)
        cancel = Gtk.Button(label=_("Cancel"))
        cancel.connect("clicked", lambda b: self.close())
        self.ok = Gtk.Button(label=_("Save"))
        self.ok.add_css_class("suggested-action")
        self.ok.connect("clicked", self.on_save)
        buttons.append(cancel)
        buttons.append(self.ok)
        box.append(buttons)
        self.update()

        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", self.on_key)
        self.add_controller(keys)
        self.connect("close-request", lambda w: self.stop_recording() or False)

    def show_option(self, value):
        child = self.option_box.get_first_child()
        if child:
            self.option_box.remove(child)
        info = ACTIONS[self.action.get_selected()]
        self.option_values = None
        self.option_widget = None
        kind = info[3]
        if kind == "command":
            w = Gtk.Entry(text=value or "", hexpand=True,
                          placeholder_text=_("A command, like: foot"))
            w.connect("changed", lambda e: self.update())
            self.option_widget = w
            title = _("Command")
        elif kind in ("choice", "desktop"):
            choices = list(info[4])
            if kind == "desktop":
                choices += [(str(i), _("desktop %d") % i)
                            for i in range(1, self.win.rc.get_int(
                                "desktops/number", 4) + 1)]
            if value and value not in [c[0] for c in choices]:
                choices.append((value, value))
            self.option_values = [c[0] for c in choices]
            w = Gtk.DropDown.new_from_strings([c[1] for c in choices])
            if value in self.option_values:
                w.set_selected(self.option_values.index(value))
            self.option_widget = w
            title = info[2].capitalize() if info[2] != "to" else _("Where")
            if info[2] == "menu":
                title = _("Menu")
            elif info[2] == "direction":
                title = _("Where")
        else:
            return
        self.option_box.append(row(title, w))
        w.set_hexpand(True)

    def option(self):
        if isinstance(self.option_widget, Gtk.Entry):
            return self.option_widget.get_text().strip()
        if isinstance(self.option_widget, Gtk.DropDown):
            return self.option_values[self.option_widget.get_selected()]
        return None

    def update(self):
        info = ACTIONS[self.action.get_selected()]
        ok = bool(self.key) and not (info[3] == "command" and not self.option())
        self.ok.set_sensitive(ok)
        other = self.win.keys.find(self.key) if self.key else None
        if other is not None and (self.binding is None or
                                  other.elem is not self.binding.elem):
            self.warning.set_label(_("%s is used for: %s. Saving replaces it.")
                                   % (key_label(self.key), other.description()))
        else:
            self.warning.set_label("")

    def start_recording(self, *args):
        self.recording = True
        self.key_button.set_label(_("Press the keys… (Escape to cancel)"))
        # owbox's own bindings would take the keys, ask it to let this
        # window have them while choosing
        surface = self.get_surface()
        if surface is not None:
            surface.inhibit_system_shortcuts(None)

    def stop_recording(self):
        if self.recording:
            self.recording = False
            surface = self.get_surface()
            if surface is not None:
                surface.restore_system_shortcuts()
        self.key_button.set_label(key_label(self.key) if self.key
                                  else _("Press to choose the keys"))

    def on_key(self, controller, keyval, keycode, state):
        if not self.recording:
            return False
        name = Gdk.keyval_name(Gdk.keyval_to_lower(keyval)) or ""
        if name in ("Shift_L", "Shift_R", "Control_L", "Control_R", "Alt_L",
                    "Alt_R", "Super_L", "Super_R", "Meta_L", "Meta_R",
                    "ISO_Level3_Shift", "Hyper_L", "Hyper_R", "Caps_Lock",
                    "Num_Lock", "ISO_Next_Group", "ISO_Prev_Group"):
            return True
        mods = state & Gtk.accelerator_get_default_mod_mask()
        if name == "Escape" and not mods:
            self.stop_recording()
            return True
        self.key = key_from_event(
            name,
            bool(mods & Gdk.ModifierType.CONTROL_MASK),
            bool(mods & Gdk.ModifierType.ALT_MASK),
            bool(mods & Gdk.ModifierType.SHIFT_MASK),
            bool(mods & Gdk.ModifierType.SUPER_MASK))
        self.stop_recording()
        self.update()
        return True

    def on_save(self, *args):
        info = ACTIONS[self.action.get_selected()]
        self.win.save_binding(self.binding, self.key, info[0], self.option())
        self.close()


def owbox_outputs():
    """The monitors, from the running owbox"""
    import json
    try:
        r = subprocess.run(["owbox", "--msg", "outputs"], capture_output=True,
                           text=True, timeout=3)
        return [json.loads(l) for l in r.stdout.splitlines() if l.strip()]
    except (OSError, subprocess.SubprocessError, ValueError):
        return []


def _icon_dirs():
    dirs = [os.path.join(GLib.get_user_data_dir(), "icons"),
            os.path.expanduser("~/.icons")]
    return dirs + [os.path.join(d, "icons") for d in
                   GLib.get_system_data_dirs()]


def icon_themes():
    found = set()
    for d in _icon_dirs():
        try:
            for name in os.listdir(d):
                index = os.path.join(d, name, "index.theme")
                if os.path.isfile(index) and name not in ("default", "hicolor"):
                    with open(index, errors="replace") as f:
                        if "Directories=" in f.read():
                            found.add(name)
        except OSError:
            pass
    return sorted(found, key=str.lower)


def cursor_themes():
    found = set()
    for d in _icon_dirs():
        try:
            for name in os.listdir(d):
                if os.path.isdir(os.path.join(d, name, "cursors")):
                    found.add(name)
        except OSError:
            pass
    return sorted(found, key=str.lower)


def themes():
    """The installed Openbox themes"""
    dirs = [os.path.join(GLib.get_user_data_dir(), "themes"),
            os.path.expanduser("~/.themes")]
    dirs += [os.path.join(d, "themes") for d in GLib.get_system_data_dirs()]
    dirs += [os.path.join(d, "owbox", "themes") for d in
             GLib.get_system_data_dirs()]
    found = set()
    for d in dirs:
        try:
            for name in os.listdir(d):
                if os.path.isfile(os.path.join(d, name, "openbox-3",
                                               "themerc")):
                    found.add(name)
        except OSError:
            pass
    return sorted(found, key=str.lower)


def theme_preview(name):
    """A picture of the theme's title bars and menu, made by
    owbox-theme-render and kept in the cache"""
    cache = os.path.join(GLib.get_user_cache_dir(), "owbox", "theme-previews")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, name + ".png")
    if os.path.isfile(path):
        return path
    render = shutil.which("owbox-theme-render") or os.path.join(
        os.path.dirname(__file__), "..", "..", "build", "owbox-theme-render")
    try:
        subprocess.run([render, name, path, "300"], capture_output=True,
                       timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return path if os.path.isfile(path) else None


class SettingsApp(Gtk.Application):
    def __init__(self, path=None):
        super().__init__(application_id=APP_ID,
                         flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.path = path

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
    return SettingsApp(path).run([sys.argv[0]])
