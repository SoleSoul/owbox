# rcxml.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""Reading and changing an Openbox/owbox rc.xml without disturbing the rest
of it: comments, sections the settings don't know, and the indentation are
kept, and new elements are indented like their neighbours."""

import os
import shutil
import subprocess
import xml.etree.ElementTree as ET

NS = "http://openbox.org/3.4/rc"
XI = "http://www.w3.org/2001/XInclude"


def q(tag):
    """The namespaced tag of an rc.xml element"""
    return "{%s}%s" % (NS, tag)


def config_home():
    return os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")


def config_dirs():
    dirs = os.environ.get("XDG_CONFIG_DIRS") or "/etc/xdg"
    return [config_home()] + [d for d in dirs.split(":") if d]


def find_config(name="rc.xml"):
    """The file owbox reads: each config directory is tried with owbox's
    and then Openbox's subdirectory, like owbox does"""
    for d in config_dirs():
        for domain in ("owbox", "openbox"):
            path = os.path.join(d, domain, name)
            if os.path.isfile(path):
                return path
    return None


def running_config():
    """The rc.xml the running owbox loaded, or None"""
    try:
        r = subprocess.run(["owbox", "--msg", "config-file"],
                           capture_output=True, text=True, timeout=3)
        path = r.stdout.strip()
        if r.returncode == 0 and path and os.path.isfile(path):
            return path
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def owbox_config_path(name="rc.xml"):
    return os.path.join(config_home(), "owbox", name)


def reconfigure():
    """Ask owbox to load the configuration (it also notices the file
    changing by itself)"""
    try:
        subprocess.run(["owbox", "--reconfigure"], capture_output=True,
                       timeout=3)
    except (OSError, subprocess.SubprocessError):
        pass


class RcXml:
    def __init__(self, path):
        self.path = path
        ET.register_namespace("", NS)
        ET.register_namespace("xi", XI)
        parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        self.tree = ET.parse(path, parser)
        self.root = self.tree.getroot()
        # the comments before the root element, which the tree can't hold
        with open(path, encoding="utf-8") as f:
            text = f.read()
        start = text.find("?>") + 2 if text.startswith("<?xml") else 0
        end = text.find("<openbox_config", start)
        self.prolog = text[start:end] if end >= 0 else "\n"

    # ---- the file ----

    def save(self, path=None):
        path = path or self.path
        tmp = path + ".owbox-settings.tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>')
            f.write(self.prolog)
            f.write(ET.tostring(self.root, encoding="unicode"))
            f.write("\n")
        os.replace(tmp, path)
        self.path = path

    def is_user_file(self):
        return os.path.realpath(self.path).startswith(
            os.path.realpath(config_home()) + os.sep)

    def is_openbox_file(self):
        return os.sep + "openbox" + os.sep in self.path

    def copy_to_owbox(self):
        """Keep owbox's settings apart from Openbox's: copy the file (and
        the menu next to it) to ~/.config/owbox and edit that from now on"""
        dest = owbox_config_path()
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        menu = os.path.join(os.path.dirname(self.path), "menu.xml")
        if os.path.isfile(menu) and not os.path.exists(
                owbox_config_path("menu.xml")):
            shutil.copy(menu, owbox_config_path("menu.xml"))
        self.save(dest)

    # ---- finding and making elements ----

    @staticmethod
    def _parse_step(step):
        """'font[place=ActiveWindow]' -> ('font', {'place': 'ActiveWindow'})"""
        attrs = {}
        if "[" in step:
            step, rest = step.split("[", 1)
            for a in rest.rstrip("]").split(","):
                k, v = a.split("=", 1)
                attrs[k.strip()] = v.strip()
        return step, attrs

    @staticmethod
    def _child(parent, tag, attrs):
        for c in parent:
            if c.tag == q(tag) and all(c.get(k) == v for k, v in attrs.items()):
                return c
        return None

    def find(self, path, parent=None):
        """The element at a path like 'theme/font[place=ActiveWindow]/size',
        or None"""
        e = self.root if parent is None else parent
        for step in path.split("/"):
            tag, attrs = self._parse_step(step)
            e = self._child(e, tag, attrs)
            if e is None:
                return None
        return e

    @staticmethod
    def _indent_of(parent):
        """The whitespace before parent's children, and before its end tag"""
        if len(parent):
            inner = parent.text if parent.text and not parent.text.strip() \
                else "\n  "
            outer = parent[-1].tail if parent[-1].tail is not None and \
                not parent[-1].tail.strip() else "\n"
            return inner, outer
        # no children yet, guess from the parent's own indentation
        return None, None

    def append(self, parent, tag, attrs=None, text=None, depth=None):
        """Add a child at the end of parent, indented like its siblings"""
        e = ET.Element(q(tag), attrs or {})
        if text is not None:
            e.text = text
        inner, outer = self._indent_of(parent)
        if inner is None:
            # rc.xml's sections start at the first column, and each level
            # in is two spaces more
            d = depth if depth is not None else self._depth(parent)
            outer = "\n" + "  " * max(d - 1, 0)
            inner = outer + "  "
            parent.text = inner
            e.tail = outer
        else:
            parent[-1].tail = inner
            e.tail = outer
        parent.append(e)
        return e

    def _depth(self, elem):
        def walk(e, d):
            if e is elem:
                return d
            for c in e:
                r = walk(c, d + 1)
                if r is not None:
                    return r
            return None
        return walk(self.root, 0) or 0

    def ensure(self, path):
        """The element at path, made (with its parents) if missing"""
        e = self.root
        for step in path.split("/"):
            tag, attrs = self._parse_step(step)
            c = self._child(e, tag, attrs)
            if c is None:
                c = self.append(e, tag, attrs)
            e = c
        return e

    def remove(self, parent, elem):
        """Remove elem, keeping the indentation of what follows"""
        children = list(parent)
        i = children.index(elem)
        if i > 0:
            # the previous element takes over the whitespace after elem
            children[i - 1].tail = elem.tail
        elif len(children) == 1:
            parent.text = None
        parent.remove(elem)

    # ---- values ----

    def get(self, path, default=None):
        e = self.find(path)
        if e is None or e.text is None:
            return default
        return e.text.strip()

    def set(self, path, value):
        e = self.ensure(path)
        e.text = str(value)

    def get_bool(self, path, default=False):
        v = self.get(path)
        if v is None:
            return default
        return v.lower() in ("yes", "true", "on", "1")

    def set_bool(self, path, value):
        self.set(path, "yes" if value else "no")

    def get_int(self, path, default=0):
        try:
            return int(self.get(path))
        except (TypeError, ValueError):
            return default

    def unset(self, path):
        """Remove the element at path, so the default is used"""
        parts = path.rsplit("/", 1)
        parent = self.find(parts[0]) if len(parts) == 2 else self.root
        e = self.find(path)
        if parent is not None and e is not None:
            self.remove(parent, e)
