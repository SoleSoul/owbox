# panels.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The panel program started with the session: by its systemd service when
it has one (waybar.service), or else from owbox's autostart."""

import shutil

from . import autostart, services

# panels that are layer-shell bars, so owbox's panels mode works with them
KNOWN_PANELS = [("waybar", "waybar"), ("sfwbar", "sfwbar"),
                ("yambar", "yambar"), ("ironbar", "ironbar"),
                ("nwg-panel", "nwg-panel"), ("hybrid-bar", "hybrid-bar"),
                ("ashell", "ashell")]


def installed():
    """The known panels that are installed: [(command, name)]"""
    return [p for p in KNOWN_PANELS if shutil.which(p[0])]


def service_of(cmd):
    """The panel's service, if it has one"""
    if not cmd or " " in cmd:
        return None
    return services.find(cmd + ".service")


def current():
    """The command of the panel started with the session, and its service
    if that starts it"""
    for cmd, name in KNOWN_PANELS:
        svc = service_of(cmd)
        if svc is not None and svc.enabled:
            return cmd, svc
    return autostart.panel(), None
