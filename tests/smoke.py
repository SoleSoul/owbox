#!/usr/bin/env python3
"""A smoke test of owbox on the headless backend.

    tests/smoke.py [--keep]
    OWBOX_ARGS=--debug-focus tests/smoke.py --keep   (more owbox options)
    OWBOX_BUILD=build-asan tests/smoke.py            (another build directory)

Runs build/owbox with data/rc.xml, opens windows with foot (and an X
application when one is installed), drives them with build/owbox-input and
checks the results with "owbox --msg windows".  --keep leaves the output
directory (log, screenshots) in place.  Exits non-zero on failure.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.abspath(os.environ.get("OWBOX_BUILD",
                                       os.path.join(ROOT, "build")))
OWBOX = os.path.join(BUILD, "owbox")
INPUT = os.path.join(BUILD, "owbox-input")

failures = []


def run(out, *cmd, timeout=20, check=False):
    return subprocess.run([os.path.join(HERE, "run.sh"), out, *cmd],
                          capture_output=True, text=True, timeout=timeout,
                          check=check)


def windows(out):
    r = run(out, OWBOX, "--msg", "windows", timeout=5)
    return [json.loads(line) for line in r.stdout.splitlines() if line]


def wait_windows(out, n, timeout=6.0):
    end = time.time() + timeout
    while time.time() < end:
        w = windows(out)
        if len(w) == n:
            return w
        time.sleep(0.2)
    return windows(out)


def inp(out, *args):
    run(out, INPUT, *[str(a) for a in args])


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name +
          ("" if cond else "  " + str(detail)))
    if not cond:
        failures.append(name)


def title_point(w):
    # the middle of the titlebar, below the top border
    return w["x"] + w["width"] // 2, w["y"] + 10


def test_xdg(out):
    subprocess.Popen([os.path.join(HERE, "run.sh"), out, "foot"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wait_windows(out, 1)
    check("foot maps", len(w) == 1, w)
    if not w:
        return
    w0 = w[0]
    check("new window is focused", w0["focused"], w0)

    x, y = title_point(w0)
    inp(out, "move", x, y, "dclick", "left", "sleep", 400)
    w = windows(out)[0]
    check("double click maximizes", w["maximized"], w)
    x, y = title_point(w)
    inp(out, "move", x, y, "dclick", "left", "sleep", 400)
    w = windows(out)[0]
    check("double click restores", not w["maximized"] and
          (w["x"], w["y"]) == (w0["x"], w0["y"]), w)

    inp(out, "key", "super+Left", "sleep", 400)
    w = windows(out)[0]
    check("W-Left snaps to the left half", w["x"] == 0 and w["y"] == 0, w)
    inp(out, "key", "super+Down", "sleep", 400)
    w = windows(out)[0]
    check("W-Down unsnaps", (w["x"], w["y"]) == (w0["x"], w0["y"]), w)

    x, y = w0["x"] + w0["width"] // 2, w0["y"] + w0["height"] // 2
    inp(out, "keydown", "Alt_L", "move", x, y, "down", "left",
        "drag", x - 100, y + 50, "up", "left", "keyup", "Alt_L", "sleep", 300)
    w = windows(out)[0]
    check("Alt-drag moves", (w["x"], w["y"]) ==
          (w0["x"] - 100, w0["y"] + 50), w)

    test_shade_decorate(out)

    inp(out, "key", "alt+F4")
    w = wait_windows(out, 0)
    check("A-F4 closes", len(w) == 0, w)


def pixel(out, x, y):
    """The color at x,y in a new screenshot, as (r, g, b)"""
    import cairo
    path = os.path.join(out, "pixel.png")
    run(out, "grim", path, timeout=10)
    img = cairo.ImageSurface.create_from_png(path)
    data, stride = img.get_data(), img.get_stride()
    b, g, r = data[y * stride + x * 4: y * stride + x * 4 + 3]
    return (r, g, b)


def action(out, xml):
    run(out, OWBOX, "--msg", "action " + xml, timeout=5)
    time.sleep(0.5)


def test_shade_decorate(out):
    w = windows(out)[0]
    desktop = (0x30, 0x30, 0x30)
    inside = (w["x"] + w["width"] // 2, w["y"] + w["height"] // 2)
    title = title_point(w)
    title_color = pixel(out, *title)

    action(out, '<action name="ToggleShade"/>')
    check("shading hides the window's contents",
          windows(out)[0]["shaded"] and pixel(out, *inside) == desktop,
          pixel(out, *inside))
    action(out, '<action name="ToggleShade"/>')
    check("unshading shows them again", pixel(out, *inside) != desktop)

    action(out, '<action name="ToggleDecorations"/>')
    check("undecorating removes the titlebar",
          pixel(out, *title) != title_color, pixel(out, *title))
    action(out, '<action name="ToggleDecorations"/>')
    check("redecorating brings it back", pixel(out, *title) == title_color,
          pixel(out, *title))


def test_scale(out):
    """<owbox><scale> makes the frame larger and keeps the window's size"""
    rc = os.path.join(out, "rc.xml")
    orig = open(rc).read()
    subprocess.Popen([os.path.join(HERE, "run.sh"), out, "foot"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wait_windows(out, 1)
    if not w:
        check("foot maps", False)
        return
    w1 = w[0]
    with open(rc, "w") as f:
        f.write(orig.replace("<scale>1</scale>", "<scale>2</scale>"))
    time.sleep(1.5)  # reloaded on change
    w2 = windows(out)[0]
    # the borders exactly, the title bar about twice (fonts are hinted)
    check("scale 2 doubles the frame",
          w2["frame"][0] == 2 * w1["frame"][0] and
          w2["frame"][3] == 2 * w1["frame"][3] and
          1.7 <= w2["frame"][1] / w1["frame"][1] <= 2.1,
          (w1["frame"], w2["frame"]))
    check("scale 2 keeps the window's size",
          (w2["width"], w2["height"]) == (w1["width"], w1["height"]), w2)
    with open(rc, "w") as f:
        f.write(orig)
    time.sleep(1.5)
    check("scale 1 restores the title bar",
          windows(out)[0]["frame"] == w1["frame"], windows(out)[0]["frame"])
    inp(out, "key", "alt+F4")
    wait_windows(out, 0)


def outputs(out):
    r = run(out, OWBOX, "--msg", "outputs", timeout=5)
    return [json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]


def test_idle(out):
    """<owbox><idle>: the monitors turn off, lock, and wake on input"""
    rc = os.path.join(out, "rc.xml")
    orig = open(rc).read()
    flag = os.path.join(out, "locked")
    with open(rc, "w") as f:
        f.write(orig.replace("<screenOff>10</screenOff>",
                             "<screenOff>0.05</screenOff>")
                    .replace("<lockCommand></lockCommand>",
                             "<lockCommand>touch %s</lockCommand>" % flag))
    time.sleep(1.5)  # reloaded on change
    deadline = time.time() + 8
    while time.time() < deadline and outputs(out)[0]["power"]:
        time.sleep(0.5)
    o = outputs(out)[0]
    check("idle turns the monitors off", not o["power"] and o["enabled"], o)
    check("and locks the screen", os.path.exists(flag))
    inp(out, "move", "200", "200")
    time.sleep(0.3)
    check("input turns them on", outputs(out)[0]["power"], outputs(out)[0])
    with open(rc, "w") as f:
        f.write(orig)
    time.sleep(1.5)


# keeps the screen on through org.freedesktop.ScreenSaver, prints the cookie,
# lets it go on each line read, quits at the end of its input
SCREENSAVER_CLIENT = r"""
import sys
from gi.repository import Gio, GLib
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
def call(method, args, reply):
    return bus.call_sync("org.freedesktop.ScreenSaver",
                         "/org/freedesktop/ScreenSaver",
                         "org.freedesktop.ScreenSaver", method, args, reply,
                         Gio.DBusCallFlags.NONE, 3000, None)
cookie = call("Inhibit", GLib.Variant("(ss)", ("smoke", "testing")),
              GLib.VariantType("(u)")).unpack()[0]
print(cookie, flush=True)
for line in sys.stdin:
    call("UnInhibit", GLib.Variant("(u)", (cookie,)), None)
    print("done", flush=True)
"""


def test_screensaver(out):
    """Applications keep the monitors on through D-Bus
    (org.freedesktop.ScreenSaver), with an owbox on its own session bus"""
    if not shutil.which("dbus-daemon"):
        print("skip org.freedesktop.ScreenSaver, no dbus-daemon")
        return
    bus = subprocess.run(["dbus-daemon", "--session", "--fork",
                          "--print-address=1", "--print-pid=1"],
                         capture_output=True, text=True, check=True)
    address, bus_pid = bus.stdout.split()
    o = os.path.join(out, "screensaver")
    os.mkdir(o)
    rc = os.path.join(o, "rc.xml")
    with open(rc, "w") as f:
        f.write(open(os.path.join(out, "rc.xml")).read()
                .replace("<screenOff>10</screenOff>",
                         "<screenOff>0.05</screenOff>")
                .replace("<lockCommand></lockCommand>",
                         "<lockCommand>true</lockCommand>"))
    env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address,
               OWBOX_TEST_SCREENSAVER="1")
    clients = []

    def client():
        p = subprocess.Popen([sys.executable, "-c", SCREENSAVER_CLIENT],
                             env=env, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        clients.append(p)
        p.stdout.readline()
        return p

    def wait_off():
        deadline = time.time() + 8
        while time.time() < deadline and outputs(o)[0]["power"]:
            time.sleep(0.5)
        return not outputs(o)[0]["power"]

    try:
        subprocess.run([os.path.join(HERE, "headless.sh"), o,
                        "--config-file", rc], env=env, check=True)
        time.sleep(0.5)
        c = client()
        time.sleep(5)
        check("Inhibit keeps the monitors on", outputs(o)[0]["power"],
              outputs(o)[0])
        c.stdin.write("\n")
        c.stdin.flush()
        c.stdout.readline()
        check("UnInhibit lets them turn off", wait_off())
        r = subprocess.run(["gdbus", "call", "--session", "--dest",
                            "org.freedesktop.ScreenSaver", "--object-path",
                            "/org/freedesktop/ScreenSaver", "--method",
                            "org.freedesktop.ScreenSaver.GetActive"],
                           env=env, capture_output=True, text=True,
                           timeout=5)
        check("and GetActive says so", r.stdout.strip() == "(true,)",
              r.stdout + r.stderr)
        inp(o, "move", "200", "200")
        time.sleep(0.3)
        c = client()
        time.sleep(1)
        c.stdin.close()
        c.wait(timeout=5)
        check("an application that quits doesn't keep them on", wait_off())
    finally:
        for c in clients:
            if c.poll() is None:
                c.kill()
        subprocess.run([os.path.join(HERE, "stop.sh"), o])
        os.kill(int(bus_pid), signal.SIGTERM)
        log = open(os.path.join(o, "owbox.log"), errors="replace").read()
        bad = [l for l in log.splitlines() if "CRITICAL" in l or
               "Sanitizer" in l and "ERROR" in l or "runtime error:" in l]
        check("no critical messages or sanitizer reports in its log",
              not bad, bad[:3])


def test_panels(out):
    """Tapping Super shows the panels over the dimmed windows, Escape and
    clicks end it, Super with another key isn't a tap"""
    subprocess.Popen([os.path.join(HERE, "run.sh"), out, "foot"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wait_windows(out, 1)
    if not w:
        check("foot maps", False)
        return
    w = w[0]
    inside = (w["x"] + w["width"] // 2, w["y"] + w["height"] // 2)
    before = pixel(out, *inside)
    inp(out, "keydown", "Super_L", "keyup", "Super_L")
    time.sleep(0.5)
    dimmed = pixel(out, *inside)
    check("tapping Super dims the windows", sum(dimmed) < sum(before),
          (before, dimmed))
    inp(out, "key", "Escape")
    time.sleep(0.5)
    check("Escape ends it", pixel(out, *inside) == before,
          pixel(out, *inside))
    inp(out, "keydown", "Super_L", "keyup", "Super_L")
    time.sleep(0.5)
    inp(out, "move", str(inside[0]), str(inside[1]), "click", "left")
    time.sleep(0.5)
    check("a click ends it", pixel(out, *inside) == before,
          pixel(out, *inside))
    # a key that isn't bound, so nothing else changes the picture
    inp(out, "keydown", "Super_L", "key", "F12", "keyup", "Super_L")
    time.sleep(0.5)
    check("Super with a key isn't a tap", pixel(out, *inside) == before,
          pixel(out, *inside))
    inp(out, "key", "alt+F4")
    wait_windows(out, 0)


def test_bars(out):
    """The example waybar's bars take their space from maximized windows,
    and auto-hidden ones don't"""
    if not shutil.which("waybar"):
        print("skip bars: no waybar")
        return
    ex = os.path.join(ROOT, "data", "examples", "waybar")
    log = open(os.path.join(out, "waybar.log"), "w")
    bar = subprocess.Popen([os.path.join(HERE, "run.sh"), out, "waybar",
                            "-c", os.path.join(ex, "config.jsonc"),
                            "-s", os.path.join(ex, "style.css")],
                           stdout=log, stderr=subprocess.STDOUT)
    subprocess.Popen([os.path.join(HERE, "run.sh"), out, "foot"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wait_windows(out, 1)
    if not w:
        check("foot maps", False)
        bar.terminate()
        return
    action(out, '<action name="Maximize"/>')
    screen = outputs(out)[0]
    # the bars come when waybar is ready, and the window makes room
    deadline = time.time() + 8
    while time.time() < deadline and bar.poll() is None and \
            windows(out)[0]["y"] == 0:
        time.sleep(0.5)
    if bar.poll() is not None:
        check("waybar runs", False,
              open(os.path.join(out, "waybar.log")).read()[-300:])
        return
    w = windows(out)[0]
    bottom = w["y"] + w["frame"][1] + w["height"] + w["frame"][3]
    check("a maximized window fits between the bars",
          w["y"] > 0 and bottom < screen["height"], (w, bottom))

    # a bar's popups (tooltips, tray menus) are made without a parent, which
    # they get from the layer surface
    below = (screen["width"] // 2, 60)
    without = pixel(out, *below)
    inp(out, "move", str(screen["width"] // 2), "12", "sleep", "1500",
        "run", "grim %s" % os.path.join(out, "tooltip.png"))
    import cairo
    img = cairo.ImageSurface.create_from_png(os.path.join(out, "tooltip.png"))
    d, st = img.get_data(), img.get_stride()
    b, g, r = d[below[1] * st + below[0] * 4:below[1] * st + below[0] * 4 + 3]
    check("a bar's tooltip shows", (r, g, b) != without, ((r, g, b), without))
    inp(out, "move", "10", "300")

    rc = os.path.join(out, "rc.xml")
    orig = open(rc).read()
    with open(rc, "w") as f:
        f.write(orig.replace("<autoHide>no</autoHide>",
                             "<autoHide>yes</autoHide>"))
    time.sleep(1.5)  # reloaded on change
    w = windows(out)[0]
    bottom = w["y"] + w["frame"][1] + w["height"] + w["frame"][3]
    check("with autoHide it has the whole screen",
          w["y"] == 0 and bottom == screen["height"], (w, bottom))
    with open(rc, "w") as f:
        f.write(orig)
    time.sleep(1.5)
    inp(out, "key", "alt+F4")
    wait_windows(out, 0)
    bar.terminate()


def test_menus(out):
    # the root menu at 700,200 (Clearlooks, the default font): Applications
    # is the 3rd item (y+62), Windows the 4th (y+84), the submenu opens at
    # x+160 with its first item at y+64
    alive = lambda: subprocess.run(
        ["kill", "-0", open(os.path.join(out, "pid")).read().strip()]
    ).returncode == 0
    # crossing another entry on the way into a submenu, then moving through
    # the submenu's entries, used to free the submenu under a pending timer
    moves = []
    for y in (264, 286, 308, 330, 352, 374, 396, 374, 352, 330, 308):
        moves += ["move", 880, y]
    inp(out, "move", 700, 200, "click", "right", "sleep", 300,
        "move", 740, 262, "sleep", 400, "move", 760, 284, *moves,
        "sleep", 600,
        # Escape closes one menu at a time, the root menu is open with a
        # submenu and its submenu
        "key", "Escape", "key", "Escape", "key", "Escape", "sleep", 200)
    check("diagonal moves into a submenu", alive())


def open_foot(out, n):
    """Open a foot window and wait until there are n windows. Returns the
    process and the new window."""
    before = {w["id"] for w in windows(out)}
    p = subprocess.Popen([os.path.join(HERE, "run.sh"), out, "foot"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    new = [w for w in wait_windows(out, n) if w["id"] not in before]
    return p, (new[0] if new else None)


def window(out, wid):
    return next((w for w in windows(out) if w["id"] == wid), None)


def desktops(out):
    r = run(out, OWBOX, "--msg", "desktops", timeout=5)
    return json.loads(r.stdout.splitlines()[0])


def test_openbox(out):
    """Openbox's own features: Alt-Tab, the window menu, desktops, moving
    and resizing with the keyboard, resistance, GrowToEdge, the dialog for
    a window that isn't responding"""
    p1, a = open_foot(out, 1)
    _, b = open_foot(out, 2)
    if not a or not b:
        check("two foot windows map", False, windows(out))
        return
    screen = outputs(out)[0]
    middle = (screen["width"] // 2, screen["height"] // 2)

    # Alt-Tab shows the list while Alt is held, and focuses the other window
    shot = os.path.join(out, "alttab.png")
    plain = pixel(out, *middle)
    inp(out, "keydown", "Alt_L", "key", "Tab", "sleep", 300,
        "run", "grim " + shot, "keyup", "Alt_L", "sleep", 300)
    import cairo
    img = cairo.ImageSurface.create_from_png(shot)
    d, st = img.get_data(), img.get_stride()
    o = middle[1] * st + middle[0] * 4
    check("Alt-Tab shows the window list", tuple(d[o:o + 3][::-1]) != plain)
    check("Alt-Tab focuses the other window", window(out, a["id"])["focused"])

    # the window menu, with the keyboard
    inp(out, "key", "alt+space", "sleep", 300, "key", "x", "sleep", 500)
    check("the window menu maximizes", window(out, a["id"])["maximized"])
    inp(out, "key", "alt+space", "sleep", 300, "key", "e", "sleep", 500)
    w = window(out, a["id"])
    check("and restores", not w["maximized"] and
          (w["x"], w["y"]) == (a["x"], a["y"]), w)
    inp(out, "key", "alt+space", "sleep", 300, "key", "s", "sleep", 300,
        "key", "Down", "key", "Return", "sleep", 500)
    check("it sends the window to desktop 2",
          window(out, a["id"])["desktop"] == 1 and
          desktops(out)["current"] == 0, (window(out, a["id"]), desktops(out)))
    action(out, '<action name="GoToDesktop"><to>2</to></action>')
    check("GoToDesktop switches to it", desktops(out)["current"] == 1 and
          window(out, a["id"])["focused"], desktops(out))
    action(out, '<action name="SendToDesktop"><to>1</to></action>')
    check("SendToDesktop brings it back and follows it",
          window(out, a["id"])["desktop"] == 0 and
          desktops(out)["current"] == 0, desktops(out))

    # moving and resizing with the keyboard, Escape puts it back
    action(out, '<action name="Move"/>')
    inp(out, "key", "Right", "key", "Down", "key", "Return", "sleep", 400)
    w = window(out, a["id"])
    check("keyboard move", w["x"] > a["x"] and w["y"] > a["y"], w)
    moved = w
    action(out, '<action name="Move"/>')
    inp(out, "key", "Right", "key", "Escape", "sleep", 400)
    w = window(out, a["id"])
    check("Escape cancels it", (w["x"], w["y"]) == (moved["x"], moved["y"]), w)
    # the first key picks the edge; foot rounds to whole cells, larger
    # than a step
    action(out, '<action name="Resize"/>')
    inp(out, "key", "Right", "key", "Right", "key", "Right", "key", "Right",
        "sleep", 300, "key", "Return", "sleep", 500)
    w = window(out, a["id"])
    check("keyboard resize", w["width"] > a["width"] and
          w["x"] == moved["x"], w)

    # resistance at the screen's edge: a little past it stays at it
    def drag_left_by(dx):
        action(out, '<action name="MoveResizeTo"><x>%d</x><y>%d</y></action>'
               % (a["x"], a["y"]))
        x, y = middle
        inp(out, "keydown", "Alt_L", "move", x, y, "down", "left",
            "drag", x - 50, y, "drag", x - dx + 5, y, "drag", x - dx, y,
            "up", "left", "keyup", "Alt_L", "sleep", 300)
        return window(out, a["id"])["x"]
    x = drag_left_by(a["x"] + 8)
    check("the screen's edge resists", x == 0, x)
    x = drag_left_by(a["x"] + 48)
    check("and lets go further on", x == -48, x)

    # GrowToEdge stops at the other window's edge, then the screen's. foot
    # rounds to whole cells, so it stops short of the first edge, and the
    # second grow goes on from the edge it was asked to reach.
    action(out, '<action name="MoveResizeTo"><x>0</x><y>0</y></action>')
    rights = []
    for i in range(2):
        action(out, '<action name="GrowToEdge"><direction>east</direction>'
               '</action>')
        w = window(out, a["id"])
        rights.append(w["x"] + w["frame"][0] + w["width"] + w["frame"][2])
    check("GrowToEdge grows to the next edge, then the screen's",
          a["width"] < rights[0] < rights[1] and
          screen["width"] - 10 < rights[1] <= screen["width"], rights)

    # a window that doesn't answer pings gets Openbox's dialog when closed,
    # which goes away when it answers again
    pid = p1.pid  # run.sh execs foot
    os.kill(pid, signal.SIGSTOP)
    try:
        inp(out, "key", "alt+F4")
        deadline = time.time() + 12
        while time.time() < deadline and len(windows(out)) < 3:
            time.sleep(0.5)
        w = windows(out)
        check("a window that doesn't respond gets the kill dialog",
              len(w) == 3 and
              "Not Responding" in window(out, a["id"])["title"], w)
    finally:
        os.kill(pid, signal.SIGCONT)
    w = wait_windows(out, 1)
    check("which goes when it responds, and it closes", len(w) == 1 and
          w[0]["id"] == b["id"], w)
    inp(out, "key", "alt+F4")
    wait_windows(out, 0)


def test_openbox_placement(out):
    """<owbox><placement>openbox</placement>: Openbox's Smart placement,
    the middle of an empty screen, then corners"""
    rc = os.path.join(out, "rc.xml")
    orig = open(rc).read()
    with open(rc, "w") as f:
        f.write(orig.replace("<placement>cascade</placement>",
                             "<placement>openbox</placement>"))
    time.sleep(1.5)  # reloaded on change
    screen = outputs(out)[0]
    placed = [open_foot(out, n)[1] for n in (1, 2, 3)]
    with open(rc, "w") as f:
        f.write(orig)
    time.sleep(1.5)
    if None in placed:
        check("three foot windows map", False, windows(out))
    else:
        def corner(w):
            right = w["x"] + w["frame"][0] + w["width"] + w["frame"][2]
            bottom = w["y"] + w["frame"][1] + w["height"] + w["frame"][3]
            return (w["x"] == 0 or right == screen["width"]) and \
                (w["y"] == 0 or bottom == screen["height"])
        check("Openbox's placement puts the next windows in corners",
              not corner(placed[0]) and corner(placed[1]) and
              corner(placed[2]), placed)
    for n in (2, 1, 0):
        inp(out, "key", "alt+F4")
        wait_windows(out, n)


def test_modifier_taps(out):
    """A tap of two modifiers (Ctrl+Super) runs in either order, and not
    with another key in between"""
    rc = os.path.join(out, "rc.xml")
    orig = open(rc).read()
    flag = os.path.join(out, "tapped")
    with open(rc, "w") as f:
        f.write(orig.replace("<keyboard>", """<keyboard>
  <keybind key="C-Super_L" tap="yes">
    <action name="Execute"><command>touch %s</command></action>
  </keybind>""" % flag, 1))
    time.sleep(1.5)  # reloaded on change

    def tapped(*keys):
        if os.path.exists(flag):
            os.remove(flag)
        inp(out, *keys)
        time.sleep(0.5)
        return os.path.exists(flag)
    check("Ctrl then Super taps", tapped(
        "keydown", "Control_L", "keydown", "Super_L", "keyup", "Super_L",
        "keyup", "Control_L"))
    check("Super then Ctrl taps", tapped(
        "keydown", "Super_L", "keydown", "Control_L", "keyup", "Control_L",
        "keyup", "Super_L"))
    check("a key in between isn't a tap", not tapped(
        "keydown", "Super_L", "keydown", "Control_L", "key", "F12",
        "keyup", "Control_L", "keyup", "Super_L"))
    with open(rc, "w") as f:
        f.write(orig)
    time.sleep(1.5)


def test_x11(out):
    if not os.path.exists(os.path.join(out, "xdisplay")):
        print("skip X applications: no Xwayland")
        return
    for cmd in (["xterm"], ["xeyes"]):
        if shutil.which(cmd[0]):
            break
    else:
        print("skip X applications: no xterm or xeyes")
        return
    subprocess.Popen([os.path.join(HERE, "run.sh"), out, *cmd],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wait_windows(out, 1, timeout=10)
    check("X application maps", len(w) == 1, w)
    if not w:
        return
    w0 = w[0]
    x, y = title_point(w0)
    inp(out, "move", x, y, "down", "left", "drag", x + 50, y + 40,
        "up", "left", "sleep", 300)
    w = windows(out)[0]
    check("X window moves", (w["x"], w["y"]) ==
          (w0["x"] + 50, w0["y"] + 40), w)
    inp(out, "key", "alt+F4")
    w = wait_windows(out, 0)
    check("X window closes", len(w) == 0, w)


def main():
    keep = "--keep" in sys.argv
    out = tempfile.mkdtemp(prefix="owbox-smoke-")
    # Super's launcher does nothing, so the tests don't depend on one
    env = dict(os.environ, OWBOX=OWBOX, OWBOX_LAUNCHER="true")
    # the default configuration, with the default menu
    rc = open(os.path.join(ROOT, "data", "rc.xml")).read()
    rc = rc.replace("<file>menu.xml</file>", "<file>%s</file>" %
                    os.path.join(ROOT, "data", "menu.xml"))
    with open(os.path.join(out, "rc.xml"), "w") as f:
        f.write(rc)
    subprocess.run([os.path.join(HERE, "headless.sh"), out,
                    "--config-file", os.path.join(out, "rc.xml"),
                    *os.environ.get("OWBOX_ARGS", "").split()],
                   env=env, check=True)
    pid = int(open(os.path.join(out, "pid")).read())
    try:
        test_xdg(out)
        test_scale(out)
        test_idle(out)
        test_screensaver(out)
        test_panels(out)
        test_bars(out)
        test_menus(out)
        test_openbox(out)
        test_openbox_placement(out)
        test_modifier_taps(out)
        test_x11(out)
        try:
            os.kill(pid, 0)
            alive = True
        except OSError:
            alive = False
        check("owbox is still running", alive)
    finally:
        subprocess.run([os.path.join(HERE, "stop.sh"), out])
        log = open(os.path.join(out, "owbox.log"), errors="replace").read()
        bad = [l for l in log.splitlines() if "CRITICAL" in l]
        check("no critical messages in the log", not bad, bad[:3])
        # reports of a build with -Db_sanitize=address,undefined, the
        # leaks' at exit
        bad = [l for l in log.splitlines()
               if "Sanitizer" in l and "ERROR" in l or "runtime error:" in l]
        check("no sanitizer reports in the log", not bad, bad[:3])
        if keep or failures:
            print("output in", out)
        else:
            shutil.rmtree(out, ignore_errors=True)
    print("%d failed" % len(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
