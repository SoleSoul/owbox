#!/usr/bin/env python3
"""Test screen sharing: run a ScreenCast portal session and count the frames.

    tools/screencast-test.py TYPES SECONDS

TYPES is 1 for monitors, 2 for windows, 3 for both.  The portal asks what to
share with its chooser (owbox's menu, "owbox --choose-share").  This needs
xdg-desktop-portal, xdg-desktop-portal-wlr and WirePlumber running, and
build/owbox-pwcount.  Frames only come when the screen changes."""
import os, subprocess, sys
import gi
from gi.repository import Gio, GLib

types, seconds = int(sys.argv[1]), sys.argv[2]
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
sender = bus.get_unique_name()[1:].replace(".", "_")
loop = GLib.MainLoop()
n = [0]

def request(method, args, opts_index):
    n[0] += 1
    token = "owboxtest%d" % n[0]
    path = "/org/freedesktop/portal/desktop/request/%s/%s" % (sender, token)
    result = {}
    def on_response(conn, s, p, i, sig, params):
        result["code"], result["res"] = params.unpack()
        loop.quit()
    sub = bus.signal_subscribe("org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request", "Response", path, None,
        Gio.DBusSignalFlags.NONE, on_response)
    args[opts_index]["handle_token"] = GLib.Variant("s", token)
    sig = {"CreateSession": "(a{sv})", "SelectSources": "(oa{sv})",
           "Start": "(osa{sv})"}[method]
    bus.call_sync("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", method, GLib.Variant(sig, tuple(args)),
        None, Gio.DBusCallFlags.NONE, -1, None)
    GLib.timeout_add_seconds(30, loop.quit)
    loop.run()
    bus.signal_unsubscribe(sub)
    print(method, "->", result.get("code"), flush=True)
    return result.get("res", {})

r = request("CreateSession", [{"session_handle_token": GLib.Variant("s", "owboxsess")}], 0)
session = r["session_handle"]
request("SelectSources", [session, {"types": GLib.Variant("u", types),
                                    "multiple": GLib.Variant("b", False)}], 1)
r = request("Start", [session, "", {}], 2)
streams = r.get("streams", [])
print("streams", streams, flush=True)
if not streams:
    sys.exit(1)
node = streams[0][0]
ret, fds = bus.call_with_unix_fd_list_sync("org.freedesktop.portal.Desktop",
    "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
    "OpenPipeWireRemote", GLib.Variant("(oa{sv})", (session, {})), None,
    Gio.DBusCallFlags.NONE, -1, None, None)
fd = fds.get(ret.unpack()[0])
import json
serial = ""
for o in json.loads(subprocess.run(["pw-dump"], capture_output=True, text=True).stdout):
    if o.get("id") == node:
        serial = str(o["info"]["props"].get("object.serial", ""))
print("serial", serial, flush=True)
subprocess.run([os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "..", "build", "owbox-pwcount"),
                str(fd if not os.environ.get("NOFD") else -1), str(node), seconds, serial], pass_fds=[fd])
