# xkb.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The keyboard layouts and options XKB knows (xkeyboard-config's
registry), with their names in words."""

import os
import xml.etree.ElementTree as ET

REGISTRY = "/usr/share/X11/xkb/rules/evdev.xml"


class Registry:
    def __init__(self, path=REGISTRY):
        self.layouts = []  # (layout, variant, description)
        self.names = {}
        self.options = []  # (option, description) of the layout switch group
        if not os.path.isfile(path):
            return
        root = ET.parse(path).getroot()
        for lay in root.iter("layout"):
            ci = lay.find("configItem")
            name = ci.findtext("name")
            desc = ci.findtext("description") or name
            self.layouts.append((name, "", desc))
            self.names[(name, "")] = desc
            for var in lay.iter("variant"):
                vci = var.find("configItem")
                vname = vci.findtext("name")
                vdesc = vci.findtext("description") or vname
                self.layouts.append((name, vname, vdesc))
                self.names[(name, vname)] = vdesc
        for group in root.iter("group"):
            gci = group.find("configItem")
            if gci is None or gci.findtext("name") != "grp":
                continue
            for opt in group.iter("option"):
                oci = opt.find("configItem")
                self.options.append((oci.findtext("name"),
                                     oci.findtext("description") or
                                     oci.findtext("name")))
        self.layouts.sort(key=lambda l: l[2].lower())

    def describe(self, layout, variant=""):
        return self.names.get((layout, variant or ""),
                              layout + (" (%s)" % variant if variant else ""))

    def all_layouts(self):
        return self.layouts

    def switch_options(self):
        """The ways to switch between layouts, like grp:alt_shift_toggle"""
        return [o for o in self.options if o[0].endswith("toggle")] + \
            [o for o in self.options if not o[0].endswith("toggle")]
