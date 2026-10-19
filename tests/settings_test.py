#!/usr/bin/env python3
"""Tests of owbox-settings' rc.xml editing (no GTK needed).

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

    # owbox reads the result without complaints
    owbox = os.path.join(ROOT, "build", "owbox")
    out = os.path.join(tmp, "run")
    subprocess.run([os.path.join(HERE, "headless.sh"), out,
                    "--config-file", path], env=dict(os.environ, OWBOX=owbox),
                   check=True)
    subprocess.run(["sleep", "1"])
    log = open(os.path.join(out, "owbox.log"), errors="replace").read()
    subprocess.run([os.path.join(HERE, "stop.sh"), out])
    check("owbox loads it", "Loaded config file " + path in log and
          "syntax error" not in log.lower())

    if not failures:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print("output in", tmp)
    print("%d failed" % len(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
