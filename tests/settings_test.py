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
