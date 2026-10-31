# widgets.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""Small helpers that build owbox Settings' rows and pages."""

import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402


# how long to wait after a change before saving it (ms)
SAVE_DELAY = 400


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
