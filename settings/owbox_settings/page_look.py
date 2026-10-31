# page_look.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Theme, Appearance and Applications pages."""

import os
import shutil
import subprocess
from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Pango", "1.0")
from gi.repository import Gdk, GLib, Gtk, Pango  # noqa: E402

from .rcxml import reconfigure  # noqa: E402
from . import desktop  # noqa: E402
from .widgets import label, row, section, page_box, SAVE_DELAY  # noqa: E402


class LookPages:
    """Theme, Appearance and Applications"""

    # ---- Appearance ----

    def page_theme(self):
        scroll, box = page_box()
        box.append(label(_("The look of window frames, menus and popups.  "
                           "Openbox themes work as they are."),
                         css="dim-label"))
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
        return scroll

    def page_appearance(self):
        scroll, box = page_box()
        box.append(section(_("Size")))
        box.append(row(_("Size of owbox"), self.scale_dropdown(),
                       _("Window frames, menus and popups.  Applications "
                         "keep their size, the Displays page scales "
                         "everything.")))

        box.append(section(_("Panels")))
        box.append(row(_("Hide the panels until Super is tapped"),
                       self.switch("owbox/panels/autoHide", False),
                       _("Bars along the screen's edges (like waybar) come up "
                         "with the launcher when Super is pressed alone, and "
                         "go with Escape, a click elsewhere or switching to "
                         "a window")))
        dims = [("0", _("No")), ("20", _("A little")), ("40", _("Some")),
                ("60", _("Much"))]
        box.append(row(_("Dim the windows meanwhile"),
                       self.dropdown("owbox/panels/dim", dims, "40")))

        box.append(section(_("Title bar")))
        layout = Gtk.Entry(text=self.rc.get("theme/titleLayout", "NLIMC"))
        layout.connect("changed", lambda w: self.set("theme/titleLayout",
                                                     w.get_text().upper()))
        box.append(row(_("Buttons"), layout,
                       _("N icon, L title, I iconify, M maximize, C close, "
                         "S roll up, D all desktops, in their order.  "
                         "Applications drawing their own title bar follow "
                         "(see the Applications page).")))

        box.append(section(_("Desktop")))
        own = self.own_themes_row()
        if own:
            box.append(own)
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

    # ---- Applications ----

    def page_applications(self):
        scroll, box = page_box()
        box.append(label(_("The look of applications.  GTK applications, and "
                           "the light or dark preference of others (libadwaita, "
                           "Electron, browsers), follow these settings, also "
                           "under GNOME and other desktops."),
                         css="dim-label"))
        if not self.desktop.available():
            box.append(label(_("gsettings-desktop-schemas is not installed: "
                               "only applications running on X follow these "
                               "settings."), css="error"))

        box.append(section(_("Style")))
        box.append(row(_("GTK theme"), self.desktop_dropdown(
            "gtk-theme", [(t, t) for t in desktop.gtk_themes()]),
            _("GTK 3 applications.  GTK 4 applications made with libadwaita "
              "have their own look, and follow the preference below.")))
        box.append(row(_("Light or dark"), self.desktop_dropdown(
            "color-scheme", [("default", _("As the application likes")),
                             ("prefer-dark", _("Dark")),
                             ("prefer-light", _("Light"))]),
            _("The preference of applications that have both")))

        box.append(section(_("Icons and cursor")))
        box.append(row(_("Icon theme"), self.desktop_dropdown(
            "icon-theme", [(t, t) for t in icon_themes()])))
        box.append(row(_("Cursor theme"), self.desktop_dropdown(
            "cursor-theme", [(t, t) for t in cursor_themes()])))
        size = Gtk.SpinButton.new_with_range(16, 96, 8)
        size.set_value(self.desktop.get("cursor-size") or 24)
        size.connect("value-changed", lambda w: self.desktop_set(
            "cursor-size", int(w.get_value())))
        box.append(row(_("Cursor size"), size))

        box.append(section(_("Fonts")))
        box.append(row(_("Interface"), self.desktop_font("font-name")))
        box.append(row(_("Monospace"),
                       self.desktop_font("monospace-font-name"),
                       _("Terminals and code")))

        box.append(section(_("Title bars drawn by applications")))
        follow = Gtk.Switch(active=desktop.titlebars_follow())
        follow.connect("notify::active", self.on_titlebars_follow)
        box.append(row(_("Like owbox's title bars"), follow,
                       _("GTK 4 and libadwaita applications draw their own "
                         "title bar.  This gives it owbox's buttons, on the "
                         "same sides, and what double, middle and right "
                         "clicks on owbox's title bars do.  It writes "
                         "button-layout and action-*-click-titlebar of "
                         "org.gnome.desktop.wm.preferences (GNOME's "
                         "too), and GTK's settings.ini.  Turning it off "
                         "leaves them as they are.")))
        self.titlebar_values = label("", css="dim-label")
        self.titlebar_values.set_selectable(True)
        box.append(self.titlebar_values)

        box.append(section(_("Qt applications")))
        box.append(self.qt_row())
        return scroll

    def sync_titlebars(self):
        """Make the title bars of applications like owbox's, when they
        follow them"""
        values = desktop.titlebar_settings(self.rc)
        follow = desktop.titlebars_follow()
        if follow:
            desktop.set_titlebar_settings(values)
        self.titlebar_values.set_label(
            (_("Written: %s") if follow else _("Would write: %s")) %
            ", ".join("%s '%s'" % (desktop.TITLEBAR_KEYS[k][0], v)
                      for k, v in values.items()))

    def on_titlebars_follow(self, w, p):
        desktop.set_titlebars_follow(w.get_active())
        self.sync_titlebars()

    def desktop_set(self, key, value):
        """Write a desktop setting, and let owbox follow it (its cursor and
        icons) once the changes stop"""
        self.desktop.set(key, value)
        if self.desktop_id:
            GLib.source_remove(self.desktop_id)
        self.desktop_id = GLib.timeout_add(SAVE_DELAY, self.desktop_done)

    def desktop_done(self):
        self.desktop_id = 0
        reconfigure()
        return False

    def desktop_dropdown(self, key, choices):
        """choices: [(value, label)], with the current value added when it
        isn't one of them"""
        cur = self.desktop.get(key)
        values = [c[0] for c in choices]
        labels = [c[1] for c in choices]
        if cur not in values:
            values.append(cur)
            labels.append(cur)
        dd = Gtk.DropDown.new_from_strings(labels)
        dd.set_selected(values.index(cur))
        dd.connect("notify::selected", lambda w, p: self.desktop_set(
            key, values[w.get_selected()]))
        return dd

    def desktop_font(self, key):
        button = Gtk.FontDialogButton(dialog=Gtk.FontDialog())
        button.set_font_desc(Pango.FontDescription.from_string(
            self.desktop.get(key)))
        button.set_level(Gtk.FontLevel.FONT)

        def on_font(w, p):
            d = w.get_font_desc()
            if d is not None:
                self.desktop_set(key, d.to_string())
        button.connect("notify::font-desc", on_font)
        return button

    def qt_row(self):
        """Qt applications follow this page through Qt's GTK theme, which
        owbox selects (QT_QPA_PLATFORMTHEME=gtk3) unless another is"""
        platform = os.environ.get("QT_QPA_PLATFORMTHEME", "")
        tool = platform if platform in ("qt5ct", "qt6ct") else None
        if platform in ("", "gtk3", "gtk2"):
            text = _("Qt applications follow these settings too (through "
                     "Qt's GTK theme), except for their widgets' shapes.")
        elif tool and shutil.which(tool):
            button = Gtk.Button(label=_("Open %s") % tool)
            button.connect("clicked", lambda b: subprocess.Popen(
                [tool], start_new_session=True))
            return row(_("Qt applications"), button,
                       _("They use %s's settings (QT_QPA_PLATFORMTHEME=%s), "
                         "only the icon theme comes from here.") %
                       (tool, tool))
        else:
            text = _("Qt applications use QT_QPA_PLATFORMTHEME=%s, so they "
                     "don't follow these settings%s.  Remove it from where "
                     "it is set (a shell's startup file, "
                     "~/.config/owbox/environment) and log in again; owbox "
                     "then uses Qt's GTK theme.") % (
                         platform, _(", and it isn't installed")
                         if tool else "")
        lab = label(text, css="dim-label")
        lab.set_selectable(True)
        return lab

    OWN_THEMES = ("owbox/icons/theme", "owbox/cursor/theme",
                  "owbox/cursor/size")

    def own_themes_row(self):
        """owbox's icons and cursor follow the Applications page, unless
        rc.xml has its own (from earlier versions, or written by hand)"""
        own = [(p, self.rc.get(p)) for p in self.OWN_THEMES
               if self.rc.get(p)]
        if not own:
            return None
        names = {"owbox/icons/theme": _("icon theme %s"),
                 "owbox/cursor/theme": _("cursor theme %s"),
                 "owbox/cursor/size": _("cursor size %s")}
        button = Gtk.Button(label=_("Use the applications' ones"))
        r = row(_("Icons and cursor"), button,
                _("owbox uses its own: %s.  The Applications page sets "
                  "them for everything.") %
                ", ".join(names[p] % v for p, v in own))

        def on_clicked(b):
            self.ensure_user_file()
            for p in self.OWN_THEMES:
                self.rc.unset(p)
            self.changed()
            r.set_visible(False)
        button.connect("clicked", on_clicked)
        return r

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
