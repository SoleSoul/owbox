#!/usr/bin/env python3
"""A smoke test of owbox on the headless backend.

    tests/smoke.py [--keep]
    OWBOX_ARGS=--debug-focus tests/smoke.py --keep   (more owbox options)

Runs build/owbox with data/rc.xml, opens windows with foot (and an X
application when one is installed), drives them with build/owbox-input and
checks the results with "owbox --msg windows".  --keep leaves the output
directory (log, screenshots) in place.  Exits non-zero on failure.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OWBOX = os.path.join(ROOT, "build", "owbox")
INPUT = os.path.join(ROOT, "build", "owbox-input")

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
    env = dict(os.environ, OWBOX=OWBOX)
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
        test_menus(out)
        test_x11(out)
        try:
            os.kill(pid, 0)
            alive = True
        except OSError:
            alive = False
        check("owbox is still running", alive)
        log = open(os.path.join(out, "owbox.log"), errors="replace").read()
        bad = [l for l in log.splitlines() if "CRITICAL" in l]
        check("no critical messages in the log", not bad, bad[:3])
    finally:
        subprocess.run([os.path.join(HERE, "stop.sh"), out])
        if keep or failures:
            print("output in", out)
        else:
            shutil.rmtree(out, ignore_errors=True)
    print("%d failed" % len(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
