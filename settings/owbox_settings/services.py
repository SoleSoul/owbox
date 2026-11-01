# services.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""systemd user services that start with the graphical session (like
waybar.service), and turning them on or off with systemctl."""

import os

from .rcxml import config_home

# where systemd looks for user units, after the user's own directory
SYSTEM_DIRS = ["/etc/systemd/user", "/usr/local/lib/systemd/user",
               "/usr/lib/systemd/user"]
# the targets owbox's session starts (owbox-session.target is bound to
# graphical-session.target)
TARGETS = ("graphical-session.target", "owbox-session.target")


class Service:
    def __init__(self, name, description, wanted_by):
        self.name = name
        self.description = description
        self.wanted_by = wanted_by
        # "user" (in the user's directory), "system" (for all users, which
        # only root can change) or None
        self.enabled = None

    @property
    def program(self):
        return self.name[:-len(".service")]


def user_dir():
    return os.path.join(config_home(), "systemd", "user")


def _parse(path):
    """The unit's Description and its [Install] WantedBy"""
    desc, wanted, section = "", [], None
    try:
        with open(path, errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("["):
                    section = line
                elif "=" in line and not line.startswith(("#", ";")):
                    k, v = (s.strip() for s in line.split("=", 1))
                    if section == "[Unit]" and k == "Description":
                        desc = v
                    elif section == "[Install]" and k == "WantedBy":
                        wanted += v.split()
    except OSError:
        pass
    return desc, wanted


def _enabled(name):
    for kind, d in (("user", user_dir()), ("system", SYSTEM_DIRS[0])):
        for t in TARGETS:
            if os.path.lexists(os.path.join(d, t + ".wants", name)):
                return kind
    return None


def session_services():
    """The services that start with the session, and the installed ones that
    can (their WantedBy is the graphical session), sorted by name"""
    found = {}
    for d in [user_dir()] + SYSTEM_DIRS:
        try:
            names = sorted(os.listdir(d))
        except OSError:
            continue
        for name in names:
            if not name.endswith(".service") or "@" in name or \
                    name in found:
                continue
            path = os.path.join(d, name)
            # masked
            if os.path.realpath(path) == "/dev/null":
                found[name] = None
                continue
            desc, wanted = _parse(path)
            found[name] = Service(name, desc, wanted)
    out = []
    for s in found.values():
        if s is None:
            continue
        s.enabled = _enabled(s.name)
        if s.enabled or any(t in TARGETS for t in s.wanted_by):
            out.append(s)
    return sorted(out, key=lambda s: s.name)


def find(name):
    for s in session_services():
        if s.name == name:
            return s
    return None


def systemctl_argv(service, on):
    """The command that turns the service on (now and at each login) or
    off"""
    return ["systemctl", "--user", "enable" if on else "disable", "--now",
            service.name]
