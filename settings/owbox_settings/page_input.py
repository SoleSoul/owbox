# page_input.py for owbox-settings
# Copyright (c) 2026 owbox contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

"""The Keyboard, Mouse & Touchpad and Key Bindings pages."""

from gettext import gettext as _

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gtk  # noqa: E402

from .keybinds import (ACTIONS, key_label, key_from_event, TAP_KEYS,  # noqa: E402
                       launcher_choices, tap_from_keys)
from . import xkb  # noqa: E402
from .widgets import label, row, section, page_box  # noqa: E402


class InputPages:
    """Keyboard, Mouse & Touchpad and Key Bindings"""

    # ---- Keyboard ----

    def page_keyboard(self):
        scroll, box = page_box()
        registry = xkb.Registry()
        box.append(section(_("Layouts")))
        box.append(label(_("The first layout is the default, the others are "
                           "switched to with the key below."),
                         css="dim-label"))
        lst = Gtk.ListBox()
        lst.set_selection_mode(Gtk.SelectionMode.NONE)
        lst.add_css_class("boxed-list")
        lst.set_margin_top(6)
        box.append(lst)

        def current():
            layouts = [s.strip() for s in
                       (self.rc.get("owbox/keyboard/layout") or "").split(",")
                       if s.strip()]
            variants = (self.rc.get("owbox/keyboard/variant") or "").split(",")
            variants += [""] * (len(layouts) - len(variants))
            return list(zip(layouts, variants))

        def write(pairs):
            self.ensure_user_file()
            if pairs:
                self.rc.set("owbox/keyboard/layout",
                            ",".join(p[0] for p in pairs))
                if any(p[1] for p in pairs):
                    self.rc.set("owbox/keyboard/variant",
                                ",".join(p[1] for p in pairs))
                else:
                    self.rc.unset("owbox/keyboard/variant")
            else:
                self.rc.unset("owbox/keyboard/layout")
                self.rc.unset("owbox/keyboard/variant")
            self.changed()
            fill()

        def fill():
            child = lst.get_first_child()
            while child:
                lst.remove(child)
                child = lst.get_first_child()
            pairs = current()
            if not pairs:
                lst.append(label(_("The system's default layout"), css="dim-label"))
            for i, (lay, var) in enumerate(pairs):
                r = Gtk.Box(spacing=6)
                for side in ("start", "end", "top", "bottom"):
                    getattr(r, "set_margin_" + side)(6)
                name = label(registry.describe(lay, var))
                name.set_hexpand(True)
                r.append(name)
                for icon, tip, func in (
                        ("go-up-symbolic", _("Move up"), "up"),
                        ("go-down-symbolic", _("Move down"), "down"),
                        ("list-remove-symbolic", _("Remove"), "remove")):
                    b = Gtk.Button(icon_name=icon, tooltip_text=tip)
                    b.add_css_class("flat")
                    b.connect("clicked", self.on_layout_button, func, i,
                              current, write)
                    r.append(b)
                lst.append(r)

        add = Gtk.Button(label=_("Add a layout…"))
        add.set_halign(Gtk.Align.START)
        add.set_margin_top(6)
        add.connect("clicked", lambda b: self.choose_layout(
            registry, lambda lay, var: write(current() + [(lay, var)])))
        box.append(add)
        fill()

        box.append(section(_("Switching layouts")))
        options = [o.strip() for o in
                   (self.rc.get("owbox/keyboard/options") or "").split(",")
                   if o.strip()]
        grp = [o for o in options if o.startswith("grp:")]
        choices = [("", _("No key"))] + registry.switch_options()
        dd = Gtk.DropDown.new_from_strings([c[1] for c in choices])
        values = [c[0] for c in choices]
        if grp and grp[0] in values:
            dd.set_selected(values.index(grp[0]))

        def on_switch(w, p):
            opts = [o.strip() for o in
                    (self.rc.get("owbox/keyboard/options") or "").split(",")
                    if o.strip() and not o.strip().startswith("grp:")]
            v = values[w.get_selected()]
            if v:
                opts.append(v)
            self.ensure_user_file()
            if opts:
                self.rc.set("owbox/keyboard/options", ",".join(opts))
            else:
                self.rc.unset("owbox/keyboard/options")
            self.changed()
        dd.connect("notify::selected", on_switch)
        box.append(row(_("Switch layouts with"), dd))

        box.append(section(_("Typing")))
        box.append(row(_("Delay before a held key repeats (ms)"),
                       self.spin("owbox/keyboard/repeatDelay", 100, 2000, 600,
                                 25)))
        box.append(row(_("Repeats per second"),
                       self.spin("owbox/keyboard/repeatRate", 1, 100, 25)))
        box.append(row(_("Num Lock on at login"),
                       self.switch("owbox/keyboard/numlock", False)))
        return scroll

    def on_layout_button(self, button, func, i, current, write):
        pairs = current()
        if func == "remove":
            del pairs[i]
        elif func == "up" and i > 0:
            pairs[i - 1], pairs[i] = pairs[i], pairs[i - 1]
        elif func == "down" and i < len(pairs) - 1:
            pairs[i + 1], pairs[i] = pairs[i], pairs[i + 1]
        write(pairs)

    def choose_layout(self, registry, done):
        win = Gtk.Window(title=_("Add a layout"), transient_for=self,
                         modal=True, default_width=420, default_height=480)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(12)
        win.set_child(box)
        search = Gtk.SearchEntry()
        box.append(search)
        lst = Gtk.ListBox()
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(lst)
        box.append(scroll)
        for lay, var, desc in registry.all_layouts():
            r = Gtk.ListBoxRow()
            r.set_child(label(desc))
            r.layout = (lay, var, desc.lower())
            lst.append(r)
        lst.set_filter_func(lambda r: search.get_text().lower() in r.layout[2])
        search.connect("search-changed", lambda w: lst.invalidate_filter())

        def chosen(lb, r):
            done(r.layout[0], r.layout[1])
            win.close()
        lst.connect("row-activated", chosen)
        win.present()

    # ---- Mouse & touchpad ----

    def libinput_path(self, match, setting):
        return "owbox/libinput/device[match=%s]/%s" % (match, setting)

    def accel(self, match):
        path = self.libinput_path(match, "accelSpeed")
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, -1, 1,
                                         0.1)
        scale.set_size_request(200, -1)
        try:
            scale.set_value(float(self.rc.get(path, "0")))
        except ValueError:
            pass
        scale.add_mark(0, Gtk.PositionType.BOTTOM, None)
        scale.connect("value-changed",
                      lambda w: self.set(path, "%.1f" % w.get_value()))
        return scale

    def page_mouse(self):
        scroll, box = page_box()
        box.append(section(_("Mouse")))
        box.append(row(_("Speed"), self.accel("pointer")))
        box.append(row(_("Natural scrolling"),
                       self.switch(self.libinput_path("pointer",
                                                      "naturalScroll"), False),
                       _("The content moves with the wheel, like on a phone")))
        box.append(row(_("Left handed"),
                       self.switch(self.libinput_path("pointer", "leftHanded"),
                                   False)))
        box.append(row(_("Double click time (ms)"),
                       self.spin("mouse/doubleClickTime", 100, 1500, 500, 50)))

        box.append(section(_("Touchpad")))
        box.append(row(_("Tap to click"),
                       self.switch(self.libinput_path("touchpad", "tap"), True)))
        box.append(row(_("Natural scrolling"),
                       self.switch(self.libinput_path("touchpad",
                                                      "naturalScroll"), True)))
        box.append(row(_("Disable while typing"),
                       self.switch(self.libinput_path("touchpad",
                                                      "disableWhileTyping"),
                                   True)))
        box.append(row(_("Speed"), self.accel("touchpad")))
        return scroll

    # ---- Key bindings ----

    def page_keys(self):
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "bottom"):
            getattr(outer, "set_margin_" + side)(24)
        outer.set_margin_top(12)
        outer.append(label(_("Double click a binding to change it."),
                           css="dim-label"))
        self.defaults_box = Gtk.Box(spacing=12)
        outer.append(self.defaults_box)
        self.fill_defaults()
        self.keys_list = Gtk.ListBox()
        self.keys_list.add_css_class("boxed-list")
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(self.keys_list)
        outer.append(scroll)
        self.keys_list.connect("row-activated",
                               lambda lb, r: self.edit_binding(r.binding))
        buttons = Gtk.Box(spacing=6)
        add = Gtk.Button(label=_("Add…"))
        add.connect("clicked", lambda b: self.edit_binding(None))
        edit = Gtk.Button(label=_("Change…"))
        edit.connect("clicked", lambda b: self.selected_binding() and
                     self.edit_binding(self.selected_binding()))
        remove = Gtk.Button(label=_("Remove"))
        remove.connect("clicked", self.on_remove_binding)
        for b in (add, edit, remove):
            buttons.append(b)
        outer.append(buttons)
        self.fill_keys()
        return outer

    def fill_keys(self):
        child = self.keys_list.get_first_child()
        while child:
            self.keys_list.remove(child)
            child = self.keys_list.get_first_child()
        for b in self.keys.bindings():
            r = Gtk.ListBoxRow()
            box = Gtk.Box(spacing=12)
            for side in ("start", "end", "top", "bottom"):
                getattr(box, "set_margin_" + side)(6)
            k = label(key_label(b.key))
            k.set_size_request(180, -1)
            box.append(k)
            d = label(b.description(),
                      css=None if b.is_simple() else "dim-label")
            d.set_hexpand(True)
            box.append(d)
            r.set_child(box)
            r.binding = b
            self.keys_list.append(r)

    def fill_defaults(self):
        """An offer of owbox's default bindings the file doesn't have, which
        an Openbox rc.xml or a copy of an older owbox's lacks"""
        box = self.defaults_box
        child = box.get_first_child()
        while child:
            box.remove(child)
            child = box.get_first_child()
        missing = self.keys.missing_defaults()
        box.set_visible(bool(missing))
        if not missing:
            return
        keys = ", ".join(key_label(b.key) for b in missing)
        if self.rc.is_openbox_file():
            text = _("owbox has default bindings this Openbox file doesn't: "
                     "%s.  Use separate settings for owbox (above) to add "
                     "them.") % keys
            box.append(label(text))
            box.get_first_child().set_wrap(True)
            return
        lab = label(_("owbox has default bindings your settings don't: %s.")
                    % keys)
        lab.set_wrap(True)
        lab.set_hexpand(True)
        box.append(lab)
        add = Gtk.Button(label=_("Add Them"))
        add.set_valign(Gtk.Align.CENTER)
        add.connect("clicked", self.on_add_defaults)
        box.append(add)

    def on_add_defaults(self, *args):
        self.ensure_user_file()
        for b in self.keys.missing_defaults():
            self.keys.add_copy(b)
        self.changed()
        self.bindings_changed()
        self.fill_defaults()

    def selected_binding(self):
        r = self.keys_list.get_selected_row()
        return r.binding if r else None

    def on_remove_binding(self, *args):
        b = self.selected_binding()
        if b:
            self.ensure_user_file()
            self.keys.remove(b)
            self.changed()
            self.bindings_changed()

    def edit_binding(self, binding, action=None):
        """action: the action a new binding starts with"""
        if binding is not None and not binding.is_simple():
            self.error(_("This binding does more than the settings can show. "
                         "You can change it in %s, or remove it here.")
                       % self.rc.path)
            return
        BindingDialog(self, binding, action).present()

    def bindings_changed(self):
        self.fill_keys()
        self.fill_panels()

    def save_binding(self, binding, key, action, option):
        self.ensure_user_file()
        other = self.keys.find(key)
        if other is not None and (binding is None or
                                  other.elem is not binding.elem):
            self.keys.remove(other)
        if binding is None:
            self.keys.add(key, action, option)
        else:
            binding.key = key
            binding.set_action(action, option)
        self.changed()
        self.bindings_changed()


class BindingDialog(Gtk.Window):
    """Choose the keys and the action of a binding"""

    def __init__(self, win, binding, action=None):
        super().__init__(title=_("Key Binding"), transient_for=win,
                         modal=True, default_width=460)
        self.win = win
        self.binding = binding
        self.key = binding.key if binding else ""
        self.recording = False
        self.held = []

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(18)
        self.set_child(box)

        self.key_button = Gtk.Button(label=key_label(self.key) if self.key
                                     else _("Press to choose the keys"))
        self.key_button.connect("clicked", self.start_recording)
        box.append(row(_("Keys"), self.key_button))

        self.action = Gtk.DropDown.new_from_strings([a[1] for a in ACTIONS])
        box.append(row(_("Action"), self.action))
        self.option_box = Gtk.Box()
        box.append(self.option_box)
        names = [a[0].lower() for a in ACTIONS]
        if binding:
            self.action.set_selected(names.index(binding.action_name().lower()))
        elif action:
            self.action.set_selected(names.index(action.lower()))
        self.action.connect("notify::selected", lambda w, p:
                            self.show_option(None))
        self.show_option(binding.option() if binding else None)

        self.warning = label("", css="dim-label")
        box.append(self.warning)
        buttons = Gtk.Box(spacing=6, halign=Gtk.Align.END)
        cancel = Gtk.Button(label=_("Cancel"))
        cancel.connect("clicked", lambda b: self.close())
        self.ok = Gtk.Button(label=_("Save"))
        self.ok.add_css_class("suggested-action")
        self.ok.connect("clicked", self.on_save)
        buttons.append(cancel)
        buttons.append(self.ok)
        box.append(buttons)
        self.update()

        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", self.on_key)
        keys.connect("key-released", self.on_key_released)
        self.add_controller(keys)
        self.connect("close-request", lambda w: self.stop_recording() or False)

    def show_option(self, value):
        child = self.option_box.get_first_child()
        if child:
            self.option_box.remove(child)
        info = ACTIONS[self.action.get_selected()]
        self.option_values = None
        self.option_widget = None
        kind = info[3]
        if kind == "command":
            w = Gtk.Entry(text=value or "", hexpand=True,
                          placeholder_text=_("A command, like: foot"))
            w.connect("changed", lambda e: self.update())
            self.option_widget = w
            title = _("Command")
        elif kind == "launcher":
            # TogglePanels' launcher, none when the option is left out of a
            # binding, owbox-launcher for a new one
            if value is None and (self.binding is None or
                                  self.binding.action_name().lower() !=
                                  "togglepanels"):
                value = "owbox-launcher"
            choices = launcher_choices(value or "")
            self.option_values = [c[0] for c in choices]
            w = Gtk.DropDown.new_from_strings([c[1] for c in choices])
            w.set_selected(self.option_values.index(value or ""))
            self.option_widget = w
            title = _("Launcher")
        elif kind in ("choice", "desktop"):
            choices = list(info[4])
            if kind == "desktop":
                choices += [(str(i), _("desktop %d") % i)
                            for i in range(1, self.win.rc.get_int(
                                "desktops/number", 4) + 1)]
            if value and value not in [c[0] for c in choices]:
                choices.append((value, value))
            self.option_values = [c[0] for c in choices]
            w = Gtk.DropDown.new_from_strings([c[1] for c in choices])
            if value in self.option_values:
                w.set_selected(self.option_values.index(value))
            self.option_widget = w
            title = info[2].capitalize() if info[2] != "to" else _("Where")
            if info[2] == "menu":
                title = _("Menu")
            elif info[2] == "direction":
                title = _("Where")
        else:
            return
        self.option_box.append(row(title, w))
        w.set_hexpand(True)

    def option(self):
        if isinstance(self.option_widget, Gtk.Entry):
            return self.option_widget.get_text().strip()
        if isinstance(self.option_widget, Gtk.DropDown):
            return self.option_values[self.option_widget.get_selected()]
        return None

    def update(self):
        info = ACTIONS[self.action.get_selected()]
        ok = bool(self.key) and not (info[3] == "command" and not self.option())
        self.ok.set_sensitive(ok)
        other = self.win.keys.find(self.key) if self.key else None
        if other is not None and (self.binding is None or
                                  other.elem is not self.binding.elem):
            self.warning.set_label(_("%s is used for: %s. Saving replaces it.")
                                   % (key_label(self.key), other.description()))
        else:
            self.warning.set_label("")

    def start_recording(self, *args):
        self.recording = True
        self.held = []
        self.key_button.set_label(_("Press the keys… (Escape to cancel)"))
        # owbox's own bindings would take the keys, ask it to let this
        # window have them while choosing
        surface = self.get_surface()
        if surface is not None:
            surface.inhibit_system_shortcuts(None)

    def stop_recording(self):
        if self.recording:
            self.recording = False
            surface = self.get_surface()
            if surface is not None:
                surface.restore_system_shortcuts()
        self.key_button.set_label(key_label(self.key) if self.key
                                  else _("Press to choose the keys"))

    def on_key(self, controller, keyval, keycode, state):
        if not self.recording:
            return False
        name = Gdk.keyval_name(Gdk.keyval_to_lower(keyval)) or ""
        # modifiers alone are a key when they're released without another
        # key (a tap, like Super for the panels, or Ctrl+Super)
        if name in TAP_KEYS:
            if name not in self.held:
                self.held.append(name)
        else:
            self.held = []
        if name in ("Shift_L", "Shift_R", "Control_L", "Control_R", "Alt_L",
                    "Alt_R", "Super_L", "Super_R", "Meta_L", "Meta_R",
                    "ISO_Level3_Shift", "Hyper_L", "Hyper_R", "Caps_Lock",
                    "Num_Lock", "ISO_Next_Group", "ISO_Prev_Group"):
            return True
        mods = state & Gtk.accelerator_get_default_mod_mask()
        if name == "Escape" and not mods:
            self.stop_recording()
            return True
        self.key = key_from_event(
            name,
            bool(mods & Gdk.ModifierType.CONTROL_MASK),
            bool(mods & Gdk.ModifierType.ALT_MASK),
            bool(mods & Gdk.ModifierType.SHIFT_MASK),
            bool(mods & Gdk.ModifierType.SUPER_MASK))
        self.stop_recording()
        self.update()
        return True

    def on_key_released(self, controller, keyval, keycode, state):
        name = Gdk.keyval_name(Gdk.keyval_to_lower(keyval)) or ""
        if not self.recording or name not in self.held:
            return
        self.key = tap_from_keys(self.held)
        self.held = []
        self.stop_recording()
        self.update()

    def on_save(self, *args):
        info = ACTIONS[self.action.get_selected()]
        self.win.save_binding(self.binding, self.key, info[0], self.option())
        self.close()
