# keybinds.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""Openbox key bindings: the actions people bind, and the bindings in an
rc.xml.  A binding the settings can show and edit is a key with one action
that has at most one option; others (chains, several actions, actions with
finalactions) are kept as they are and shown as advanced."""

import copy
import os
import xml.etree.ElementTree as ET
from gettext import gettext as _

from .rcxml import q, config_dirs, RcXml

# Openbox's modifier letters, in the order they're written
MODIFIERS = [("C", _("Ctrl")), ("A", _("Alt")), ("S", _("Shift")),
             ("W", _("Super")), ("H", _("Hyper")), ("M", _("Meta")),
             ("Mod1", "Mod1"), ("Mod2", "Mod2"), ("Mod3", "Mod3"),
             ("Mod4", "Mod4"), ("Mod5", "Mod5")]

DIRECTIONS = [("left", _("left")), ("right", _("right")), ("up", _("up")),
              ("down", _("down")), ("next", _("next")),
              ("previous", _("previous"))]

EDGES = [("west", _("left half")), ("east", _("right half")),
         ("north", _("top (maximize)")), ("south", _("bottom half")),
         ("northwest", _("top left quarter")),
         ("northeast", _("top right quarter")),
         ("southwest", _("bottom left quarter")),
         ("southeast", _("bottom right quarter"))]

MENUS = [("root-menu", _("Root menu")), ("client-menu", _("Window menu")),
         ("client-list-combined-menu", _("Windows on all desktops")),
         ("applications-menu", _("Applications"))]

# (action name, label, option element, option kind, choices)
# option kinds: None, "text", "command", "desktop", "choice"
ACTIONS = [
    ("Execute", _("Run a command"), "command", "command", None),
    ("ShowMenu", _("Show a menu"), "menu", "choice", MENUS),
    ("NextWindow", _("Switch to the next window"), None, None, None),
    ("PreviousWindow", _("Switch to the previous window"), None, None, None),
    ("GoToDesktop", _("Go to a desktop"), "to", "desktop", DIRECTIONS),
    ("SendToDesktop", _("Send the window to a desktop"), "to", "desktop",
     DIRECTIONS),
    ("Close", _("Close the window"), None, None, None),
    ("Iconify", _("Iconify (minimize) the window"), None, None, None),
    ("ToggleMaximize", _("Maximize or restore the window"), None, None, None),
    ("ToggleFullscreen", _("Make the window fullscreen or not"), None, None,
     None),
    ("ToggleShade", _("Roll the window up or down"), None, None, None),
    ("ToggleDecorations", _("Remove or restore the window's frame"), None,
     None, None),
    ("ToggleOmnipresent", _("Show the window on all desktops or one"), None,
     None, None),
    ("ToggleAlwaysOnTop", _("Keep the window above others or not"), None,
     None, None),
    ("SnapToEdge", _("Snap the window to the screen's edge"), "direction",
     "choice", EDGES),
    ("Unsnap", _("Unsnap the window"), None, None, None),
    ("TogglePanels", _("Show the panels, and a launcher"), "launcher",
     "text", None),
    ("Raise", _("Raise the window"), None, None, None),
    ("Lower", _("Lower the window"), None, None, None),
    ("ToggleShowDesktop", _("Show the desktop"), None, None, None),
    ("Move", _("Move the window with the keyboard"), None, None, None),
    ("Resize", _("Resize the window with the keyboard"), None, None, None),
    ("Kill", _("Kill the window's program"), None, None, None),
    ("Reconfigure", _("Reload the configuration"), None, None, None),
    ("Exit", _("Log out"), None, None, None),
]
ACTION_INFO = {a[0].lower(): a for a in ACTIONS}


# modifiers that can be bound alone, as a tap (tap="yes")
TAP_KEYS = {
    "Super_L": _("Super"), "Super_R": _("Right Super"),
    "Alt_L": _("Alt"), "Alt_R": _("Right Alt"),
    "Control_L": _("Ctrl"), "Control_R": _("Right Ctrl"),
    "Shift_L": _("Shift"), "Shift_R": _("Right Shift"),
}

# keys whose X names aren't words people use
KEY_NAMES = {
    "XF86AudioRaiseVolume": _("Volume Up"),
    "XF86AudioLowerVolume": _("Volume Down"),
    "XF86AudioMute": _("Mute"),
    "XF86AudioMicMute": _("Microphone Mute"),
    "XF86AudioPlay": _("Play"),
    "XF86AudioPause": _("Pause"),
    "XF86AudioStop": _("Stop"),
    "XF86AudioNext": _("Next Track"),
    "XF86AudioPrev": _("Previous Track"),
    "XF86MonBrightnessUp": _("Brightness Up"),
    "XF86MonBrightnessDown": _("Brightness Down"),
    "Print": _("Print Screen"),
    "Return": _("Enter"),
    "Prior": _("Page Up"),
    "Next": _("Page Down"),
}


def key_label(key):
    """'W-A-Left' -> 'Super+Alt+Left'"""
    if key in TAP_KEYS:
        return _("%s (alone)") % TAP_KEYS[key]
    parts = key.split("-")
    # a key that is a minus sign: 'C--'
    if key.endswith("--"):
        parts = key[:-2].split("-") + ["-"]
    names = dict(MODIFIERS)
    out = [names.get(p, p) for p in parts[:-1]]
    k = parts[-1]
    if len(k) == 1:
        k = k.upper()
    k = KEY_NAMES.get(k, k)
    return "+".join(out + [k])


def key_from_event(keyname, ctrl, alt, shift, super_):
    """The Openbox key string for a key and the modifiers held"""
    mods = [m for m, on in (("C", ctrl), ("A", alt), ("S", shift),
                            ("W", super_)) if on]
    return "-".join(mods + [keyname])


class Binding:
    """One <keybind> element"""

    def __init__(self, elem):
        self.elem = elem

    @property
    def key(self):
        return self.elem.get("key", "")

    @key.setter
    def key(self, value):
        self.elem.set("key", value)
        # a modifier alone works when it's tapped
        if value in TAP_KEYS:
            self.elem.set("tap", "yes")
        elif "tap" in self.elem.attrib:
            del self.elem.attrib["tap"]

    def actions(self):
        return [a for a in self.elem if a.tag == q("action")]

    def is_simple(self):
        """One action this knows, and no chained keys"""
        acts = self.actions()
        return len(acts) == 1 and \
            not any(c.tag == q("keybind") for c in self.elem) and \
            acts[0].get("name", "").lower() in ACTION_INFO

    def action_name(self):
        acts = self.actions()
        return acts[0].get("name", "") if acts else ""

    def _option_elem(self):
        acts = self.actions()
        if not acts:
            return None
        info = ACTION_INFO.get(acts[0].get("name", "").lower())
        if not info or not info[2]:
            return None
        names = (info[2], "execute") if info[0] == "Execute" else (info[2],)
        for name in names:
            e = acts[0].find(q(name))
            if e is not None:
                return e
        return None

    def option(self):
        e = self._option_elem()
        return e.text.strip() if e is not None and e.text else None

    def description(self):
        """What the binding does, in words"""
        if not self.is_simple():
            if any(c.tag == q("keybind") for c in self.elem):
                return _("A chain of keys (advanced)")
            names = [a.get("name", "?") for a in self.actions()]
            return ", ".join(names) + " " + _("(advanced)")
        info = ACTION_INFO[self.action_name().lower()]
        opt = self.option()
        if opt is None:
            return info[1]
        if info[4]:
            opt = dict(info[4]).get(opt, opt)
        return "%s: %s" % (info[1], opt)

    def set_action(self, name, option=None):
        """Make the binding run one action with its option.  When the action
        stays the same, its other options (like wrap or finalactions) stay
        too."""
        info = ACTION_INFO.get(name.lower())
        acts = self.actions()
        if len(acts) == 1 and acts[0].get("name", "").lower() == name.lower() \
                and not any(c.tag == q("keybind") for c in self.elem):
            act = acts[0]
            e = self._option_elem()
            if info and info[2]:
                if option in (None, ""):
                    if e is not None:
                        act.remove(e)
                elif e is not None:
                    e.text = option
                else:
                    e = ET.Element(q(info[2]))
                    e.text = option
                    act.insert(0, e)
            return
        for c in list(self.elem):
            self.elem.remove(c)
        act = ET.SubElement(self.elem, q("action"), {"name": name})
        if info and info[2] and option not in (None, ""):
            opt = ET.SubElement(act, q(info[2]))
            opt.text = option
        _indent(self.elem, self.depth)

    # <keyboard> is at the first column, a <keybind> two spaces in
    depth = 1


def _indent(elem, depth):
    """Indent a new <keybind> like rc.xml's: the action on its own line,
    and the action's options next to it"""
    pad = "\n" + "  " * depth
    if len(elem):
        elem.text = pad + "  "
        for i, c in enumerate(elem):
            c.tail = pad + "  " if i < len(elem) - 1 else pad
            for o in c:
                o.tail = None


# the bindings owbox's default rc.xml has besides Openbox's, which an Openbox
# rc.xml, or a copy of an older owbox's, doesn't have
OWBOX_DEFAULT_KEYS = [
    "Super_L", "W-Left", "W-Right", "W-Up", "W-Down", "W-e",
    "XF86AudioRaiseVolume", "XF86AudioLowerVolume", "XF86AudioMute",
    "XF86AudioMicMute", "XF86AudioPlay", "XF86AudioPause", "XF86AudioNext",
    "XF86AudioPrev", "XF86MonBrightnessUp", "XF86MonBrightnessDown",
    "Print", "A-Print", "S-Print",
]


def system_rc():
    """owbox's default rc.xml (in /etc/xdg/owbox), or None"""
    for d in config_dirs()[1:]:
        path = os.path.join(d, "owbox", "rc.xml")
        if os.path.isfile(path):
            return path
    return None


class KeyBindings:
    def __init__(self, rc):
        self.rc = rc

    def missing_defaults(self):
        """owbox's default bindings for keys this file doesn't bind, as
        bindings of the default rc.xml"""
        path = system_rc()
        if path is None or \
                os.path.realpath(path) == os.path.realpath(self.rc.path):
            return []
        try:
            defaults = KeyBindings(RcXml(path))
        except (OSError, ET.ParseError):
            return []
        out = []
        for key in OWBOX_DEFAULT_KEYS:
            b = defaults.find(key)
            if b is not None and self.find(key) is None:
                out.append(b)
        return out

    def add_copy(self, binding):
        """Add a copy of another file's binding, as it is written there"""
        kb = self.keyboard()
        e = copy.deepcopy(binding.elem)
        # take the place (and the indentation) of a new binding
        new = self.rc.append(kb, "keybind")
        e.tail = new.tail
        i = list(kb).index(new)
        kb.remove(new)
        kb.insert(i, e)
        return Binding(e)

    def keyboard(self):
        return self.rc.ensure("keyboard")

    def bindings(self):
        return [Binding(e) for e in self.keyboard() if e.tag == q("keybind")]

    def find(self, key):
        for b in self.bindings():
            if b.key.lower() == key.lower():
                return b
        return None

    def add(self, key, action, option=None):
        kb = self.keyboard()
        e = self.rc.append(kb, "keybind", {"key": key})
        b = Binding(e)
        b.key = key
        b.set_action(action, option)
        return b

    def remove(self, binding):
        self.rc.remove(self.keyboard(), binding.elem)
