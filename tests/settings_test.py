#!/usr/bin/env python3
"""Tests of owbox-settings: its rc.xml editing, and its window.

    tests/settings_test.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "settings"))

from owbox_settings.rcxml import RcXml  # noqa: E402
from owbox_settings.keybinds import KeyBindings, key_label  # noqa: E402

failures = []


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name +
          ("" if cond else "  " + str(detail)))
    if not cond:
        failures.append(name)


def main():
    tmp = tempfile.mkdtemp(prefix="owbox-settings-test-")
    path = os.path.join(tmp, "rc.xml")
    shutil.copy(os.path.join(ROOT, "data", "rc.xml"), path)
    original = open(path).read()

    rc = RcXml(path)
    check("reads the theme", rc.get("theme/name") == "Clearlooks")
    check("reads an owbox setting", rc.get_int("owbox/keyboard/repeatRate") == 25)
    check("reads an attribute path",
          rc.get("theme/font[place=ActiveWindow]/size") == "8",
          rc.get("theme/font[place=ActiveWindow]/size"))

    rc.set("theme/name", "Onyx")
    rc.set("owbox/keyboard/layout", "us,il")
    rc.set_bool("focus/followMouse", True)
    rc.set("desktops/names/name", "Work")
    kb = KeyBindings(rc)
    n = len(kb.bindings())
    kb.add("W-Return", "Execute", "foot")
    we = kb.find("W-e")
    we.set_action("Execute", "thunar")
    left = kb.find("C-A-Left")
    left.key = "W-C-Left"
    left.set_action("GoToDesktop", "left")
    kb.remove(kb.find("W-d"))
    rc.save()

    text = open(path).read()
    rc2 = RcXml(path)
    kb2 = KeyBindings(rc2)
    check("values are saved", rc2.get("theme/name") == "Onyx" and
          rc2.get("owbox/keyboard/layout") == "us,il" and
          rc2.get_bool("focus/followMouse"))
    check("bindings are added and removed", len(kb2.bindings()) == n and
          kb2.find("W-Return") and not kb2.find("W-d"))
    check("a new binding is indented like the others",
          '  <keybind key="W-Return">\n    <action name="Execute">'
          '<command>foot</command></action>\n  </keybind>' in text,
          text[text.find('W-Return') - 20:text.find('W-Return') + 120])
    check("editing an option keeps the action's other options",
          '<keybind key="W-C-Left">\n    <action name="GoToDesktop">'
          '<to>left</to><wrap>no</wrap></action>' in text)
    check("an edited command is changed", kb2.find("W-e").option() == "thunar")
    check("comments are kept", text.count("<!--") == original.count("<!--"),
          (text.count("<!--"), original.count("<!--")))
    check("the default namespace is kept",
          'xmlns="http://openbox.org/3.4/rc"' in text and "ns0:" not in text)
    check("key labels", key_label("W-A-Left") == "Super+Alt+Left" and
          key_label("C-A-d") == "Ctrl+Alt+D", key_label("C-A-d"))
    check("descriptions", kb2.find("W-Return").description() ==
          "Run a command: foot", kb2.find("W-Return").description())
    check("Alt-Tab with finalactions is editable",
          kb2.find("A-Tab").is_simple())
    rc4 = RcXml(path)
    kb4 = KeyBindings(rc4)
    kb4.add("Super_R", "TogglePanels", "fuzzel")
    rc4.save()
    b = KeyBindings(RcXml(path)).find("Super_R")
    check("a modifier alone is bound as a tap",
          b is not None and b.elem.get("tap") == "yes" and
          b.option() == "fuzzel" and key_label("Super_R") == "Right Super (alone)",
          b.elem.attrib if b is not None else None)
    check("media key labels", key_label("XF86AudioMute") == "Mute" and
          key_label("S-Print") == "Shift+Print Screen",
          key_label("S-Print"))

    # an older rc.xml, without some of owbox's default bindings, gets them
    # from the system's (XDG_CONFIG_DIRS)
    sysdir = os.path.join(tmp, "sys")
    os.makedirs(os.path.join(sysdir, "owbox"))
    shutil.copy(os.path.join(ROOT, "data", "rc.xml"),
                os.path.join(sysdir, "owbox", "rc.xml"))
    os.environ["XDG_CONFIG_DIRS"] = sysdir
    old = os.path.join(tmp, "old.xml")
    shutil.copy(os.path.join(ROOT, "data", "rc.xml"), old)
    rc3 = RcXml(old)
    kb3 = KeyBindings(rc3)
    for key in ("Print", "XF86AudioMute"):
        kb3.remove(kb3.find(key))
    kb3.find("W-e").set_action("Execute", "thunar")
    rc3.save()
    rc3 = RcXml(old)
    kb3 = KeyBindings(rc3)
    missing = [b.key for b in kb3.missing_defaults()]
    check("missing default bindings are found",
          missing == ["XF86AudioMute", "Print"], missing)
    for b in kb3.missing_defaults():
        kb3.add_copy(b)
    rc3.save()
    kb3 = KeyBindings(RcXml(old))
    check("and added, keeping the user's own",
          kb3.find("Print") is not None and not kb3.missing_defaults() and
          kb3.find("W-e").option() == "thunar")
    check("an added binding is written like the others",
          '\n  <keybind key="Print">\n    <action name="Execute">' in
          open(old).read())
    del os.environ["XDG_CONFIG_DIRS"]

    # the look of applications, with GSettings in a file of its own
    cfg = os.path.join(tmp, "config")
    os.makedirs(os.path.join(cfg, "gtk-3.0"))
    os.makedirs(os.path.join(cfg, "qt6ct"))
    with open(os.path.join(cfg, "gtk-3.0", "settings.ini"), "w") as f:
        f.write("# mine\n[Settings]\ngtk-enable-animations=false\n")
    with open(os.path.join(cfg, "qt6ct", "qt6ct.conf"), "w") as f:
        f.write("[Appearance]\nstyle=Fusion\n\n[Fonts]\nfixed=x\n")
    r = subprocess.run([sys.executable, __file__, "--desktop"],
                       env=dict(os.environ, XDG_CONFIG_HOME=cfg,
                                GSETTINGS_BACKEND="keyfile"),
                       capture_output=True, text=True)
    check("the look of applications is read back",
          r.stdout.split("\n")[0] == "WhiteSur-Dark Papirus 32 prefer-dark",
          r.stdout + r.stderr)
    check("title bars are like owbox's", r.stdout.split("\n")[1:3] ==
          ["icon:minimize,maximize,close toggle-maximize lower menu",
           "close,maximize:minimize,icon minimize none none"], r.stdout + r.stderr)
    ini = open(os.path.join(cfg, "gtk-3.0", "settings.ini")).read()
    check("and written for applications on X", "# mine" in ini and
          "gtk-enable-animations=false" in ini and
          "gtk-theme-name=WhiteSur-Dark" in ini and
          "gtk-application-prefer-dark-theme=true" in ini and
          "gtk-decoration-layout=close,maximize:minimize,icon" in ini and
          "gtk-titlebar-double-click=minimize" in ini, ini)
    qt = open(os.path.join(cfg, "qt6ct", "qt6ct.conf")).read()
    check("and the icon theme for Qt", qt ==
          "[Appearance]\nstyle=Fusion\nicon_theme=Papirus\n\n"
          "[Fonts]\nfixed=x\n", qt)

    # owbox reads the result without complaints
    owbox = os.path.join(ROOT, "build", "owbox")
    out = os.path.join(tmp, "run")
    subprocess.run([os.path.join(HERE, "headless.sh"), out,
                    "--config-file", path], env=dict(os.environ, OWBOX=owbox),
                   check=True)
    subprocess.run(["sleep", "1"])
    log = open(os.path.join(out, "owbox.log"), errors="replace").read()
    check("owbox loads it", "Loaded config file " + path in log and
          "syntax error" not in log.lower())

    # owbox Settings' window, in that owbox, builds and shows every page
    cfg = os.path.join(tmp, "window-config")
    os.makedirs(cfg)
    r = subprocess.run([os.path.join(HERE, "run.sh"), out, sys.executable,
                        __file__, "--window", path],
                       env=dict(os.environ, XDG_CONFIG_HOME=cfg,
                                GSETTINGS_BACKEND="keyfile",
                                PATH=os.path.dirname(owbox) + os.pathsep +
                                os.environ["PATH"]),
                       capture_output=True, text=True, timeout=60)
    subprocess.run([os.path.join(HERE, "stop.sh"), out])
    check("the window shows every page", r.stdout.strip() ==
          "pages: theme appearance applications windows displays desktops "
          "keyboard mouse keys autostart" and "Traceback" not in r.stderr,
          r.stdout + r.stderr[-2000:])

    if not failures:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print("output in", tmp)
    print("%d failed" % len(failures) if failures else "all passed")
    return 1 if failures else 0


def desktop_main():
    """Set the look of applications, and print it as read back"""
    from gi.repository import Gio
    from owbox_settings import desktop
    d = desktop.DesktopSettings()
    d.set("gtk-theme", "WhiteSur-Dark")
    d.set("icon-theme", "Papirus")
    d.set("cursor-size", 32)
    d.set("color-scheme", "prefer-dark")
    d = desktop.DesktopSettings()
    print(d.get("gtk-theme"), d.get("icon-theme"), d.get("cursor-size"),
          d.get("color-scheme"))

    # the default rc.xml, and one with other buttons and clicks
    gs = Gio.Settings.new(desktop.WM_SCHEMA)
    for xml in (DEFAULT_RC, TITLEBAR_RC):
        path = os.path.join(os.environ["XDG_CONFIG_HOME"], "rc.xml")
        with open(path, "w") as f:
            f.write(xml)
        desktop.set_titlebar_settings(desktop.titlebar_settings(RcXml(path)))
        print(" ".join(gs.get_string(g)
                       for g, ini in desktop.TITLEBAR_KEYS.values()))
    return 0


DEFAULT_RC = open(os.path.join(ROOT, "data", "rc.xml")).read()

TITLEBAR_RC = """<?xml version="1.0"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
<theme><titleLayout>CMSLDIN</titleLayout></theme>
<mouse>
  <context name="Titlebar">
    <mousebind button="Left" action="DoubleClick">
      <action name="Raise"/><action name="Iconify"/>
    </mousebind>
    <mousebind button="Right" action="Press">
      <action name="ShowMenu"><menu>root-menu</menu></action>
    </mousebind>
  </context>
</mouse>
</openbox_config>
"""


def window_main():
    """Open owbox Settings on an rc.xml, show each page, print their names"""
    from gi.repository import Gio, GLib
    from owbox_settings import app
    a = app.SettingsApp(sys.argv[2])
    # not the user's running owbox Settings
    a.set_flags(a.get_flags() | Gio.ApplicationFlags.NON_UNIQUE)
    shown = []

    def show_pages():
        win = a.props.active_window
        child = win.stack.get_first_child() if win else None
        while child:
            win.stack.set_visible_child(child)
            while GLib.MainContext.default().iteration(False):
                pass
            shown.append(win.stack.get_page(child).get_name())
            child = child.get_next_sibling()
        print("pages:", " ".join(shown))
        a.quit()

    a.connect("activate", lambda *args: GLib.idle_add(show_pages))
    return a.run([sys.argv[0]])


if __name__ == "__main__":
    if "--desktop" in sys.argv:
        sys.exit(desktop_main())
    if "--window" in sys.argv:
        sys.exit(window_main())
    sys.exit(main())
