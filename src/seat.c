/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   seat.c for owbox
   Copyright (c) 2026        owbox contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "seat.h"
#include "server.h"
#include "xwayland.h"
#include "event.h"
#include "client.h"
#include "frame.h"
#include "window.h"
#include "config.h"
#include "openbox.h"
#include "grab.h"
#include "focus.h"
#include "layers.h"
#include "menuframe.h"
#include "mouse.h"
#include "screen.h"
#include "debug.h"
#include "obt/keyboard.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <libinput.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>

typedef struct {
    struct wl_list link;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
} ObKeyboard;

typedef struct {
    struct wl_list link;
    struct wlr_input_device *device;
    struct wl_listener destroy;
} ObPointerDevice;

typedef struct {
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener set_region;
    struct wl_listener destroy;
} ObConstraint;

typedef struct {
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
} ObShortcutsInhibitor;

/* What happens to a pointer button press */
typedef enum {
    PRESS_NONE,
    /*! owbox used the press, it keeps the pointer until all buttons are up
      (like an X passive grab) */
    PRESS_OWBOX,
    /*! The press went to a client, which keeps the pointer until all
      buttons are up */
    PRESS_CLIENT
} ObPressOwner;

static struct {
    struct wl_list keyboards; /* ObKeyboard::link */
    struct wl_list pointers;  /* ObPointerDevice::link */

    gboolean kgrab;
    gboolean pgrab;

    /*! A surface that holds the keyboard instead of the focused client, for
      layer shell surfaces with keyboard interactivity and unmanaged
      Xwayland windows */
    struct wlr_surface *override_focus;
    /*! The client that has keyboard focus (or would, under override) */
    ObClient *focus;

    /*! Keys that owbox used, their release doesn't go to the client */
    GHashTable *used_keys;

    /*! X style button numbers held down, as a bitmask */
    guint buttons;
    ObPressOwner press_owner;
    /*! A press held back for the client until mouse.c says to replay it */
    gboolean replay_pending;
    guint32 replay_time;
    guint32 replay_button;
    gboolean replay_is_axis;
    struct wlr_pointer_axis_event replay_axis;

    /*! Where the press that owbox holds the pointer for happened, events go
      there until the buttons are released (an X implicit grab) */
    ObClient *press_client;
    ObFWin *press_win;
    ObNode *press_node;

    /*! The client under the pointer, for enter/leave */
    ObClient *pointer_client;
    /*! Accumulated smooth scrolling, turned into button 4-7 clicks */
    double scroll_acc[2];

    gboolean locked;

    struct wlr_pointer_constraint_v1 *active_constraint;
    GList *shortcut_inhibitors;
    GList *idle_inhibitors;

    const gchar *cursor_name;
    gboolean cursor_from_client;
    /* the xcursor shown, owbox's or a client's shape, NULL when a client
       shows a surface */
    const gchar *xcursor_name;

    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener swipe_begin, swipe_update, swipe_end;
    struct wl_listener pinch_begin, pinch_update, pinch_end;
    struct wl_listener hold_begin, hold_end;
    struct wl_listener touch_down, touch_up, touch_motion, touch_cancel,
        touch_frame;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_shape;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener new_constraint;
    struct wl_listener new_shortcuts_inhibitor;
    struct wl_listener new_idle_inhibitor;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;
} seat;

static void pointer_update(guint32 time, gboolean motion);
static void update_constraint(void);

/* ---- helpers ---- */

static guint32 now_msec(void)
{
    return server_now_msec();
}

static void notify_activity(void)
{
    if (server.idle_notifier)
        wlr_idle_notifier_v1_notify_activity(server.idle_notifier,
                                             server.seat);
}

static guint current_modifiers(void)
{
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server.seat);
    return kb ? wlr_keyboard_get_modifiers(kb) : 0;
}

/* Linux button codes to X button numbers */
static guint linux_to_x_button(guint32 button)
{
    switch (button) {
    case BTN_LEFT: return 1;
    case BTN_MIDDLE: return 2;
    case BTN_RIGHT: return 3;
    case BTN_SIDE: return 8;
    case BTN_EXTRA: return 9;
    case BTN_FORWARD: return 9;
    case BTN_BACK: return 8;
    default:
        if (button >= BTN_MOUSE && button < BTN_MOUSE + 16)
            return 10 + (button - BTN_TASK);
    }
    return 0;
}

static guint x_button_mask(guint button)
{
    return button >= 1 && button <= 5 ? OB_BUTTON1_MASK << (button - 1) : 0;
}

static guint event_state(void)
{
    guint state = current_modifiers();
    guint b;
    for (b = 1; b <= 5; ++b)
        if (seat.buttons & (1u << b))
            state |= x_button_mask(b);
    return state;
}

/* The client a scene node belongs to and the frame element, if any */
static ObClient* node_client(ObNode *node, ObFWin **fwin)
{
    if (fwin) *fwin = NULL;
    if (!node || !node->owner || !WINDOW_IS_CLIENT(node->owner))
        return NULL;
    if (node->type == OB_NODE_FRAME_PART && fwin)
        *fwin = (ObFWin*)node;
    else if (node->type == OB_NODE_WINDOW && fwin &&
             node != &WINDOW_AS_CLIENT(node->owner)->node)
        /* the frame's top level window */
        *fwin = (ObFWin*)node;
    return WINDOW_AS_CLIENT(node->owner);
}

/* Position of a frame element in the layout */
static void fwin_root_pos(ObFWin *w, gint *x, gint *y)
{
    wlr_scene_node_coords(&w->tree->node, x, y);
}

/* ---- cursor ---- */

static const gchar* cursor_name(ObCursor c)
{
    switch (c) {
    case OB_CURSOR_NONE:
    case OB_CURSOR_POINTER: return "default";
    case OB_CURSOR_BUSYPOINTER: return "progress";
    case OB_CURSOR_BUSY: return "wait";
    case OB_CURSOR_MOVE: return "move";
    case OB_CURSOR_NORTH: return "n-resize";
    case OB_CURSOR_NORTHEAST: return "ne-resize";
    case OB_CURSOR_EAST: return "e-resize";
    case OB_CURSOR_SOUTHEAST: return "se-resize";
    case OB_CURSOR_SOUTH: return "s-resize";
    case OB_CURSOR_SOUTHWEST: return "sw-resize";
    case OB_CURSOR_WEST: return "w-resize";
    case OB_CURSOR_NORTHWEST: return "nw-resize";
    case OB_NUM_CURSORS: break;
    }
    return "default";
}

void seat_set_cursor(ObCursor cursor)
{
    const gchar *name = cursor_name(cursor);
    if (!seat.cursor_from_client && seat.cursor_name &&
        !strcmp(seat.cursor_name, name))
        return;
    seat.cursor_name = name;
    seat.cursor_from_client = FALSE;
    seat.xcursor_name = name;
    wlr_cursor_set_xcursor(server.cursor, server.xcursor_manager, name);
}

void seat_update_cursor(void)
{
    pointer_update(now_msec(), FALSE);
}

/* ---- keyboard focus ---- */

static void send_keyboard_enter(struct wlr_surface *surface)
{
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server.seat);

    if (!surface) {
        wlr_seat_keyboard_notify_clear_focus(server.seat);
        return;
    }
    if (kb)
        wlr_seat_keyboard_notify_enter(server.seat, surface, kb->keycodes,
                                       kb->num_keycodes, &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(server.seat, surface, NULL, 0, NULL);
}

static void update_shortcuts_inhibit(void)
{
    GList *it;
    struct wlr_surface *focused =
        server.seat->keyboard_state.focused_surface;

    for (it = seat.shortcut_inhibitors; it; it = g_list_next(it)) {
        ObShortcutsInhibitor *i = it->data;
        if (i->inhibitor->surface == focused &&
            config_allow_shortcuts_inhibit)
            wlr_keyboard_shortcuts_inhibitor_v1_activate(i->inhibitor);
        else
            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(i->inhibitor);
    }
}

static gboolean shortcuts_inhibited(void)
{
    GList *it;
    for (it = seat.shortcut_inhibitors; it; it = g_list_next(it)) {
        ObShortcutsInhibitor *i = it->data;
        if (i->inhibitor->active)
            return TRUE;
    }
    return FALSE;
}

gboolean seat_focus_client(ObClient *client)
{
    ObClient *old = seat.focus;

    if (seat.locked)
        return FALSE;

    seat.focus = client;
    if (!seat.override_focus)
        send_keyboard_enter(client ? client->impl->surface(client) : NULL);

    update_shortcuts_inhibit();
    update_constraint();

    /* this is the FocusIn/FocusOut handling */
    if (old != client || client == NULL)
        event_focus_changed(old, client, seat.override_focus != NULL);
    return TRUE;
}

void seat_focus_surface(struct wlr_surface *surface)
{
    ObClient *c;

    if (seat.locked && surface == NULL) {
        /* the lock screen keeps the keyboard */
        return;
    }
    seat.override_focus = surface;
    if (surface) {
        send_keyboard_enter(surface);
        /* no client has focus now, like a FocusOut to an unmanaged window */
        c = seat.focus;
        if (c)
            event_focus_changed(c, NULL, TRUE);
    } else {
        /* give it back */
        c = seat.focus;
        if (c && c->managed) {
            send_keyboard_enter(c->impl->surface(c));
            event_focus_changed(NULL, c, FALSE);
        } else {
            seat.focus = NULL;
            send_keyboard_enter(NULL);
            if (ob_state() == OB_STATE_RUNNING)
                focus_fallback(FALSE, config_focus_under_mouse, TRUE, TRUE);
        }
    }
    update_shortcuts_inhibit();
}

struct wlr_surface* seat_focused_surface(void)
{
    return server.seat->keyboard_state.focused_surface;
}

void seat_surface_gone(struct wlr_surface *surface)
{
    if (seat.override_focus == surface)
        seat_focus_surface(NULL);
}

void seat_keyboard_grabbed(gboolean grabbed)
{
    seat.kgrab = grabbed;
}

void seat_pointer_grabbed(gboolean grabbed)
{
    seat.pgrab = grabbed;
    if (grabbed) {
        /* the client loses the pointer during a grab, like the LeaveNotify
           NotifyGrab in X */
        wlr_seat_pointer_notify_clear_focus(server.seat);
        seat.press_owner = PRESS_OWBOX;
    } else if (!seat.buttons) {
        /* the grab ended with no buttons held (a menu closed with the
           keyboard), the next press starts over */
        seat.press_owner = PRESS_NONE;
        seat.press_client = NULL;
        seat.press_win = NULL;
        seat.press_node = NULL;
    }
    seat_rebase_pointer();
}

/* ---- keyboard ---- */

static guint xkb_mask_to_wlr(struct xkb_keymap *keymap, xkb_mod_mask_t mask)
{
    static const gchar *names[] = {
        XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CAPS, XKB_MOD_NAME_CTRL,
        XKB_MOD_NAME_ALT, XKB_MOD_NAME_NUM, "Mod3", XKB_MOD_NAME_LOGO,
        "Mod5"
    };
    guint i, ret = 0;
    for (i = 0; i < G_N_ELEMENTS(names); ++i) {
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(keymap, names[i]);
        if (idx != XKB_MOD_INVALID && (mask & (1u << idx)))
            ret |= 1u << i;
    }
    return ret;
}

static guint keysym_modmask(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
        return WLR_MODIFIER_SHIFT;
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
        return WLR_MODIFIER_CTRL;
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
        return WLR_MODIFIER_ALT;
    case XKB_KEY_Super_L: case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
        return WLR_MODIFIER_LOGO;
    case XKB_KEY_ISO_Level3_Shift:
        return WLR_MODIFIER_MOD5;
    case XKB_KEY_Caps_Lock:
        return WLR_MODIFIER_CAPS;
    case XKB_KEY_Num_Lock:
        return WLR_MODIFIER_MOD2;
    }
    return 0;
}

static gboolean handle_vt_switch(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (server.session)
            wlr_session_change_vt(server.session,
                                  sym - XKB_KEY_XF86Switch_VT_1 + 1);
        return TRUE;
    }
    return FALSE;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    ObKeyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *ev = data;
    struct xkb_state *xstate = kb->wlr_keyboard->xkb_state;
    struct xkb_keymap *keymap = kb->wlr_keyboard->keymap;
    ObEvent e = {0};
    gboolean used = FALSE;
    gboolean press = ev->state == WL_KEYBOARD_KEY_STATE_PRESSED;

    notify_activity();

    e.type = press ? OB_EVENT_KEY_PRESS : OB_EVENT_KEY_RELEASE;
    e.time = ev->time_msec;
    /* the xkb state is still the state from before this key */
    e.state = wlr_keyboard_get_modifiers(kb->wlr_keyboard) | event_state();
    e.keycode = ev->keycode + 8;
    if (xstate) {
        const xkb_keysym_t *syms;
        xkb_layout_index_t layout;

        e.sym = xkb_state_key_get_one_sym(xstate, e.keycode);
        layout = xkb_state_key_get_layout(xstate, e.keycode);
        if (xkb_keymap_key_get_syms_by_level(keymap, e.keycode, layout, 0,
                                             &syms) > 0)
            e.raw_sym = syms[0];
        if (xkb_keymap_key_get_syms_by_level(keymap, e.keycode, 0, 0,
                                             &syms) > 0)
            e.first_sym = syms[0];
        e.consumed_mods = xkb_mask_to_wlr(
            keymap, xkb_state_key_get_consumed_mods2(xstate, e.keycode,
                                                     XKB_CONSUMED_MODE_XKB));
        if (press)
            e.unichar = xkb_state_key_get_utf32(xstate, e.keycode);
    }
    e.modmask = keysym_modmask(e.raw_sym);
    seat_pointer_pos(&e.x_root, &e.y_root);

    if (press && handle_vt_switch(e.sym)) {
        g_hash_table_add(seat.used_keys, GUINT_TO_POINTER(e.keycode));
        return;
    }

    if (!seat.locked) {
        if (press) {
            if (seat.kgrab || !shortcuts_inhibited())
                used = event_key(&e);
            /* everything goes to owbox while it holds the keyboard */
            used = used || seat.kgrab;
            if (used)
                g_hash_table_add(seat.used_keys,
                                 GUINT_TO_POINTER(e.keycode));
        } else {
            event_key(&e);
            /* a release goes where its press went, so clients never think a
               key is stuck down */
            used = g_hash_table_remove(seat.used_keys,
                                       GUINT_TO_POINTER(e.keycode));
        }
    }

    if (!used) {
        wlr_seat_set_keyboard(server.seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server.seat, ev->time_msec,
                                     ev->keycode, ev->state);
    }
}

static void keyboard_handle_modifiers(struct wl_listener *listener,
                                      void *data)
{
    ObKeyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(server.seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(server.seat,
                                       &kb->wlr_keyboard->modifiers);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
    ObKeyboard *kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    g_free(kb);
}

static void keyboard_configure(struct wlr_keyboard *wlr_keyboard)
{
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = {
        .rules = config_xkb_rules,
        .model = config_xkb_model,
        .layout = config_xkb_layout,
        .variant = config_xkb_variant,
        .options = config_xkb_options,
    };
    struct xkb_keymap *keymap;

    /* unset names come from the XKB_DEFAULT_* environment variables */
    keymap = xkb_keymap_new_from_names(context, &names,
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        g_message("Invalid keyboard layout settings, using the defaults");
        keymap = xkb_keymap_new_from_names(context, NULL,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (keymap) {
        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        if (config_kb_numlock) {
            xkb_mod_index_t num =
                xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);
            if (num != XKB_MOD_INVALID) {
                struct wlr_keyboard_modifiers m = wlr_keyboard->modifiers;
                m.locked |= 1u << num;
                wlr_keyboard_notify_modifiers(wlr_keyboard, m.depressed,
                                              m.latched, m.locked, m.group);
            }
        }
        xkb_keymap_unref(keymap);
    }
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, config_kb_repeat_rate,
                                 config_kb_repeat_delay);
}

static void seat_update_capabilities(void)
{
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&seat.keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server.seat, caps);
}

static void new_keyboard(struct wlr_keyboard *wlr_keyboard, gboolean virtual)
{
    ObKeyboard *kb = g_new0(ObKeyboard, 1);
    kb->wlr_keyboard = wlr_keyboard;

    /* virtual keyboards come with their own keymap */
    if (!virtual)
        keyboard_configure(wlr_keyboard);

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &kb->key);
    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&wlr_keyboard->base.events.destroy, &kb->destroy);

    wlr_seat_set_keyboard(server.seat, wlr_keyboard);
    wl_list_insert(&seat.keyboards, &kb->link);
}

/* ---- libinput settings ---- */

static ObLibinputConfig* libinput_config_for(struct wlr_input_device *dev,
                                             struct libinput_device *ld)
{
    GSList *it;
    gboolean touchpad =
        libinput_device_config_tap_get_finger_count(ld) > 0;
    ObLibinputConfig *found = NULL;

    /* the last match wins, so general settings can come first */
    for (it = config_libinput; it; it = g_slist_next(it)) {
        ObLibinputConfig *lc = it->data;
        if (!lc->match || !strcmp(lc->match, "*") ||
            (!strcmp(lc->match, "touchpad") && touchpad) ||
            (!strcmp(lc->match, "pointer") && !touchpad) ||
            (dev->name && g_pattern_match_simple(lc->match, dev->name)))
            found = lc;
    }
    return found;
}

static void configure_libinput(struct wlr_input_device *dev)
{
    struct libinput_device *ld;
    ObLibinputConfig *lc;

    if (!wlr_input_device_is_libinput(dev)) return;
    ld = wlr_libinput_get_device_handle(dev);

    /* tap to click is what people expect from touchpads, libinput leaves
       it off */
    if (libinput_device_config_tap_get_finger_count(ld) > 0)
        libinput_device_config_tap_set_enabled(ld,
                                               LIBINPUT_CONFIG_TAP_ENABLED);

    if (!(lc = libinput_config_for(dev, ld))) return;

    if (lc->tap >= 0 && libinput_device_config_tap_get_finger_count(ld))
        libinput_device_config_tap_set_enabled(ld, lc->tap ?
            LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    if (lc->tap_button_map >= 0 &&
        libinput_device_config_tap_get_finger_count(ld))
        libinput_device_config_tap_set_button_map(ld, lc->tap_button_map);
    if (lc->drag >= 0 && libinput_device_config_tap_get_finger_count(ld))
        libinput_device_config_tap_set_drag_enabled(ld, lc->drag ?
            LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
    if (lc->drag_lock >= 0 && libinput_device_config_tap_get_finger_count(ld))
        libinput_device_config_tap_set_drag_lock_enabled(ld, lc->drag_lock ?
            LIBINPUT_CONFIG_DRAG_LOCK_ENABLED :
            LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
    if (lc->natural_scroll >= 0 &&
        libinput_device_config_scroll_has_natural_scroll(ld))
        libinput_device_config_scroll_set_natural_scroll_enabled(
            ld, lc->natural_scroll);
    if (lc->left_handed >= 0 &&
        libinput_device_config_left_handed_is_available(ld))
        libinput_device_config_left_handed_set(ld, lc->left_handed);
    if (lc->middle_emulation >= 0 &&
        libinput_device_config_middle_emulation_is_available(ld))
        libinput_device_config_middle_emulation_set_enabled(ld,
            lc->middle_emulation ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED :
            LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
    if (lc->disable_while_typing >= 0 &&
        libinput_device_config_dwt_is_available(ld))
        libinput_device_config_dwt_set_enabled(ld, lc->disable_while_typing ?
            LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
    if (lc->scroll_method >= 0)
        libinput_device_config_scroll_set_method(ld, lc->scroll_method);
    if (lc->click_method >= 0)
        libinput_device_config_click_set_method(ld, lc->click_method);
    if (lc->accel_profile >= 0 &&
        libinput_device_config_accel_is_available(ld))
        libinput_device_config_accel_set_profile(ld, lc->accel_profile);
    if (!isnan(lc->accel_speed) &&
        libinput_device_config_accel_is_available(ld))
        libinput_device_config_accel_set_speed(ld, lc->accel_speed);
    if (lc->send_events >= 0)
        libinput_device_config_send_events_set_mode(ld, lc->send_events);
}

/* ---- pointer devices ---- */

static void pointer_device_destroy(struct wl_listener *listener, void *data)
{
    ObPointerDevice *p = wl_container_of(listener, p, destroy);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    g_free(p);
}

static void new_pointer_device(struct wlr_input_device *device)
{
    ObPointerDevice *p = g_new0(ObPointerDevice, 1);
    p->device = device;
    p->destroy.notify = pointer_device_destroy;
    wl_signal_add(&device->events.destroy, &p->destroy);
    wl_list_insert(&seat.pointers, &p->link);

    configure_libinput(device);
    wlr_cursor_attach_input_device(server.cursor, device);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        new_keyboard(wlr_keyboard_from_input_device(device), FALSE);
        break;
    case WLR_INPUT_DEVICE_POINTER:
    case WLR_INPUT_DEVICE_TOUCH:
        new_pointer_device(device);
        break;
    default:
        break;
    }
    seat_update_capabilities();
}

static void handle_new_virtual_keyboard(struct wl_listener *listener,
                                        void *data)
{
    struct wlr_virtual_keyboard_v1 *vkb = data;
    new_keyboard(&vkb->keyboard, TRUE);
    seat_update_capabilities();
}

static void handle_new_virtual_pointer(struct wl_listener *listener,
                                       void *data)
{
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    new_pointer_device(&event->new_pointer->pointer.base);
    seat_update_capabilities();
}

/* Applications draw the cursor over their windows with the same theme */
static void export_cursor_theme(void)
{
    gchar *size = g_strdup_printf("%d", config_cursor_size);
    if (config_cursor_theme)
        g_setenv("XCURSOR_THEME", config_cursor_theme, TRUE);
    else
        g_unsetenv("XCURSOR_THEME");
    g_setenv("XCURSOR_SIZE", size, TRUE);
    g_free(size);
}

void seat_reconfigure(void)
{
    ObKeyboard *kb;
    ObPointerDevice *p;
    struct wlr_xcursor_manager *old;

    wl_list_for_each(kb, &seat.keyboards, link)
        keyboard_configure(kb->wlr_keyboard);
    wl_list_for_each(p, &seat.pointers, link)
        configure_libinput(p->device);

    old = server.xcursor_manager;
    server.xcursor_manager =
        wlr_xcursor_manager_create(config_cursor_theme, config_cursor_size);
    /* the cursor keeps the manager of the image it shows, so show that
       image from the new one before destroying the old one */
    if (seat.xcursor_name)
        wlr_cursor_set_xcursor(server.cursor, server.xcursor_manager,
                               seat.xcursor_name);
    if (old)
        wlr_xcursor_manager_destroy(old);
    export_cursor_theme();
    seat.cursor_name = NULL;
    seat_update_cursor();
    xwayland_update_cursor();
}

/* ---- pointer ---- */

void seat_pointer_pos(gint *x, gint *y)
{
    *x = (gint)floor(server.cursor->x);
    *y = (gint)floor(server.cursor->y);
}

void seat_pointer_warp(gint x, gint y)
{
    wlr_cursor_warp(server.cursor, NULL, x, y);
    pointer_update(now_msec(), TRUE);
}

gboolean seat_pointer_button_held(void)
{
    return seat.buttons != 0;
}

guint seat_pointer_held_button(void)
{
    guint b;
    for (b = 1; b < 32; ++b)
        if (seat.buttons & (1u << b))
            return b;
    return 0;
}

/* Find out what the event is over, filling in the client and frame
   element and the coordinates relative to them */
static void fill_event_target(ObEvent *e, struct wlr_surface **surface,
                              double *sx, double *sy, ObNode **node_out)
{
    ObNode *node;
    ObFWin *fwin;
    ObClient *c;

    node = window_at(server.cursor->x, server.cursor->y, surface, sx, sy);
    if (node_out) *node_out = node;
    c = node_client(node, &fwin);
    e->client = c;
    e->win = fwin;
    e->node = node;
    if (fwin) {
        gint fx, fy;
        fwin_root_pos(fwin, &fx, &fy);
        e->x = e->x_root - fx;
        e->y = e->y_root - fy;
    } else if (c && c->frame) {
        e->x = e->x_root - (c->frame->area.x + c->frame->size.left);
        e->y = e->y_root - (c->frame->area.y + c->frame->size.top);
    } else {
        e->x = e->x_root;
        e->y = e->y_root;
    }
}

/* Fill in the event for the press that owbox holds the pointer for */
static void fill_press_target(ObEvent *e)
{
    e->client = seat.press_client;
    e->win = seat.press_win;
    e->node = seat.press_node;
    if (e->win) {
        gint fx, fy;
        fwin_root_pos(e->win, &fx, &fy);
        e->x = e->x_root - fx;
        e->y = e->y_root - fy;
    } else if (e->client && e->client->frame) {
        e->x = e->x_root - (e->client->frame->area.x +
                            e->client->frame->size.left);
        e->y = e->y_root - (e->client->frame->area.y +
                            e->client->frame->size.top);
    } else {
        e->x = e->x_root;
        e->y = e->y_root;
    }
}

void seat_client_gone(ObClient *client)
{
    if (seat.press_client == client) {
        seat.press_client = NULL;
        seat.press_win = NULL;
        seat.press_node = NULL;
    }
    if (seat.pointer_client == client)
        seat.pointer_client = NULL;
    if (seat.focus == client)
        seat.focus = NULL;
}

/* Is the surface something that gets pointer input like a client but is not
   one (layer shell, unmanaged Xwayland, the lock screen)? */
static gboolean other_surface(ObNode *node, struct wlr_surface *surface)
{
    if (!surface) return FALSE;
    if (!node) return TRUE;
    if (node->owner && WINDOW_IS_LAYER(node->owner))
        return !layers_surface_is_root(node->owner);
    return node->owner == NULL || WINDOW_IS_UNMANAGED(node->owner);
}

static void pointer_update(guint32 time, gboolean motion)
{
    ObEvent e = {0};
    struct wlr_surface *surface = NULL;
    double sx = 0, sy = 0;
    ObNode *node = NULL;
    ObCursor cursor = OB_CURSOR_POINTER;
    gboolean client_surface;

    e.type = OB_EVENT_MOTION;
    e.time = time;
    e.state = event_state();
    seat_pointer_pos(&e.x_root, &e.y_root);

    /* drag and drop icons follow the pointer */
    wlr_scene_node_set_position(&server.drag_tree->node,
                                e.x_root, e.y_root);

    if (server.seat->drag) {
        /* a drag and drop sends enter/motion to whatever is under the
           pointer, the seat's drag grab takes care of the protocol */
        window_at(server.cursor->x, server.cursor->y, &surface, &sx, &sy);
        if (surface)
            wlr_seat_pointer_notify_enter(server.seat, surface, sx, sy);
        else
            wlr_seat_pointer_notify_clear_focus(server.seat);
        if (motion)
            wlr_seat_pointer_notify_motion(server.seat, time, sx, sy);
        return;
    }

    if (seat.locked) {
        node = window_at(server.cursor->x, server.cursor->y, &surface,
                         &sx, &sy);
        if (surface && layers_surface_is_lock(surface)) {
            wlr_seat_pointer_notify_enter(server.seat, surface, sx, sy);
            if (motion)
                wlr_seat_pointer_notify_motion(server.seat, time, sx, sy);
        } else {
            wlr_seat_pointer_notify_clear_focus(server.seat);
            seat_set_cursor(OB_CURSOR_POINTER);
        }
        return;
    }

    /* owbox has the pointer */
    if (seat.pgrab || seat.press_owner == PRESS_OWBOX) {
        if (seat.pgrab)
            fill_event_target(&e, NULL, NULL, NULL, NULL);
        else
            fill_press_target(&e);
        if (motion)
            event_pointer(&e);
        if (seat.pgrab && grab_pointer_cursor() != OB_CURSOR_NONE)
            seat_set_cursor(grab_pointer_cursor());
        else if (!seat.pgrab && e.win)
            seat_set_cursor(e.win->cursor);
        else if (!seat.pgrab)
            seat_set_cursor(OB_CURSOR_POINTER);
        return;
    }

    /* a client has the pointer while its buttons are held */
    if (seat.press_owner == PRESS_CLIENT &&
        server.seat->pointer_state.focused_surface)
    {
        struct wlr_surface *fs = server.seat->pointer_state.focused_surface;
        gint lx = 0, ly = 0;
        /* find where the focused surface is */
        node = window_at(server.cursor->x, server.cursor->y, &surface,
                         &sx, &sy);
        if (surface != fs) {
            struct wlr_scene_buffer *b = window_scene_buffer_for_surface(fs);
            if (b) {
                wlr_scene_node_coords(&b->node, &lx, &ly);
                sx = server.cursor->x - lx;
                sy = server.cursor->y - ly;
            }
        }
        if (motion)
            wlr_seat_pointer_notify_motion(server.seat, time, sx, sy);
        return;
    }

    fill_event_target(&e, &surface, &sx, &sy, &node);

    /* the client under the pointer changed (X Enter/LeaveNotify) */
    if (e.client != seat.pointer_client) {
        seat.pointer_client = e.client;
        if (!event_ignoring_enters() || config_focus_under_mouse)
            event_pointer_enter(e.client, motion);
    }

    /* hovering over titlebar buttons and menu entries */
    event_pointer(&e);

    client_surface = surface && e.client && !e.win;
    if (client_surface || other_surface(node, surface)) {
        wlr_seat_pointer_notify_enter(server.seat, surface, sx, sy);
        if (motion)
            wlr_seat_pointer_notify_motion(server.seat, time, sx, sy);
        /* the client sets the cursor, keep what it set */
        return;
    }

    wlr_seat_pointer_notify_clear_focus(server.seat);
    if (e.win)
        cursor = e.win->cursor;
    seat_set_cursor(cursor);
}

void seat_rebase_pointer(void)
{
    pointer_update(now_msec(), FALSE);
}

void seat_output_layout_changed(void)
{
    /* keep the pointer on an output */
    wlr_cursor_move(server.cursor, NULL, 0, 0);
    seat_rebase_pointer();
}

static void apply_constraint_motion(double *dx, double *dy)
{
    struct wlr_pointer_constraint_v1 *c = seat.active_constraint;
    struct wlr_surface *s;
    struct wlr_scene_buffer *b;
    gint lx, ly;
    double sx, sy, sx2, sy2;

    if (!c) return;
    if (c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        *dx = *dy = 0;
        return;
    }
    /* confined */
    s = c->surface;
    b = window_scene_buffer_for_surface(s);
    if (!b) return;
    wlr_scene_node_coords(&b->node, &lx, &ly);
    sx = server.cursor->x - lx;
    sy = server.cursor->y - ly;
    if (wlr_region_confine(&c->region, sx, sy, sx + *dx, sy + *dy,
                           &sx2, &sy2))
    {
        *dx = sx2 - sx;
        *dy = sy2 - sy;
    }
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct wlr_pointer_motion_event *ev = data;
    double dx = ev->delta_x, dy = ev->delta_y;

    notify_activity();
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server.relative_pointer, server.seat,
        (uint64_t)ev->time_msec * 1000, ev->delta_x, ev->delta_y,
        ev->unaccel_dx, ev->unaccel_dy);

    if (!seat.pgrab && seat.press_owner != PRESS_OWBOX)
        apply_constraint_motion(&dx, &dy);
    wlr_cursor_move(server.cursor, &ev->pointer->base, dx, dy);
    pointer_update(ev->time_msec, TRUE);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
                                          void *data)
{
    struct wlr_pointer_motion_absolute_event *ev = data;
    double lx, ly;

    notify_activity();
    wlr_cursor_absolute_to_layout_coords(server.cursor, &ev->pointer->base,
                                         ev->x, ev->y, &lx, &ly);
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server.relative_pointer, server.seat,
        (uint64_t)ev->time_msec * 1000, lx - server.cursor->x,
        ly - server.cursor->y, lx - server.cursor->x, ly - server.cursor->y);
    wlr_cursor_warp_absolute(server.cursor, &ev->pointer->base, ev->x, ev->y);
    pointer_update(ev->time_msec, TRUE);
}

void seat_pointer_replay_press(void)
{
    if (!seat.replay_pending) return;
    seat.replay_pending = FALSE;

    if (seat.replay_is_axis) {
        struct wlr_pointer_axis_event *a = &seat.replay_axis;
        wlr_seat_pointer_notify_axis(server.seat, a->time_msec,
                                     a->orientation, a->delta,
                                     a->delta_discrete, a->source,
                                     a->relative_direction);
    } else {
        wlr_seat_pointer_notify_button(server.seat, seat.replay_time,
                                       seat.replay_button,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
        seat.press_owner = PRESS_CLIENT;
    }
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
    struct wlr_pointer_button_event *ev = data;
    gboolean press = ev->state == WL_POINTER_BUTTON_STATE_PRESSED;
    guint xbutton = linux_to_x_button(ev->button);
    ObEvent e = {0};
    struct wlr_surface *surface = NULL;
    double sx, sy;
    ObNode *node = NULL;

    notify_activity();

    e.type = press ? OB_EVENT_BUTTON_PRESS : OB_EVENT_BUTTON_RELEASE;
    e.time = ev->time_msec;
    e.state = event_state();
    e.button = xbutton;
    seat_pointer_pos(&e.x_root, &e.y_root);

    if (press) seat.buttons |= 1u << MIN(xbutton, 31);
    else seat.buttons &= ~(1u << MIN(xbutton, 31));

    if (seat.locked) {
        wlr_seat_pointer_notify_button(server.seat, ev->time_msec,
                                       ev->button, ev->state);
        return;
    }

    /* owbox has the pointer (a grab, or a press it used) */
    if (seat.pgrab || seat.press_owner == PRESS_OWBOX) {
        if (seat.pgrab)
            fill_event_target(&e, NULL, NULL, NULL, NULL);
        else
            /* the events go where the press was, like in X */
            fill_press_target(&e);
        event_pointer(&e);
        if (!seat.buttons && !seat.pgrab) {
            seat.press_owner = PRESS_NONE;
            seat.press_client = NULL;
            seat.press_win = NULL;
            seat.press_node = NULL;
            seat_rebase_pointer();
        }
        return;
    }

    /* a client has the pointer */
    if (seat.press_owner == PRESS_CLIENT) {
        wlr_seat_pointer_notify_button(server.seat, ev->time_msec,
                                       ev->button, ev->state);
        if (!seat.buttons) {
            seat.press_owner = PRESS_NONE;
            seat_rebase_pointer();
        }
        return;
    }

    if (!press) {
        /* a release without its press (it went to something gone) */
        wlr_seat_pointer_notify_button(server.seat, ev->time_msec,
                                       ev->button, ev->state);
        return;
    }

    /* a new press */
    fill_event_target(&e, &surface, &sx, &sy, &node);

    if (e.client && mouse_frame_binding_exists(
            xbutton, obt_keyboard_only_modmasks(e.state)))
    {
        /* the frame's grab is above everything inside the frame */
        e.win = e.client->frame->window;
        e.x = e.x_root - e.client->frame->area.x;
        e.y = e.y_root - e.client->frame->area.y;
    }

    seat.press_client = e.client;
    seat.press_win = e.win;
    seat.press_node = e.node;

    if (e.client && !e.win) {
        /* the client context, mouse.c replays the press to the client */
        seat.replay_pending = TRUE;
        seat.replay_is_axis = FALSE;
        seat.replay_time = ev->time_msec;
        seat.replay_button = ev->button;
        if (surface)
            wlr_seat_pointer_notify_enter(server.seat, surface, sx, sy);
        event_pointer(&e);
        /* if nothing replayed it (a binding grabbed the pointer), the client
           doesn't get it, and owbox keeps the pointer */
        seat.replay_pending = FALSE;
        if (seat.press_owner == PRESS_NONE)
            seat.press_owner = PRESS_OWBOX;
    } else if (other_surface(node, surface)) {
        /* a panel or such, it is not managed by Openbox's rules but menus
           still close when clicking elsewhere */
        if (menu_frame_visible || seat.kgrab)
            event_pointer(&e);
        if (seat.pgrab) {
            seat.press_owner = PRESS_OWBOX;
        } else {
            wlr_seat_pointer_notify_button(server.seat, ev->time_msec,
                                           ev->button, ev->state);
            seat.press_owner = PRESS_CLIENT;
            layers_surface_clicked(surface);
        }
    } else {
        /* a frame element or the root window */
        event_pointer(&e);
        seat.press_owner = PRESS_OWBOX;
        wlr_seat_pointer_notify_clear_focus(server.seat);
    }
    if (!seat.buttons && seat.press_owner != PRESS_NONE && !seat.pgrab) {
        seat.press_owner = PRESS_NONE;
        seat_rebase_pointer();
    }
}

/* Scrolling makes X button 4-7 clicks, which Openbox binds to things like
   switching desktops on the root window */
static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
    struct wlr_pointer_axis_event *ev = data;
    gint axis = ev->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL ? 0 : 1;
    gint clicks = 0;
    ObEvent e = {0};
    struct wlr_surface *surface = NULL;
    double sx, sy;
    ObNode *node = NULL;
    gboolean forwarded = FALSE;
    gint i;

    notify_activity();

    if (seat.locked || seat.press_owner == PRESS_CLIENT) {
        wlr_seat_pointer_notify_axis(server.seat, ev->time_msec,
                                     ev->orientation, ev->delta,
                                     ev->delta_discrete, ev->source,
                                     ev->relative_direction);
        return;
    }

    /* turn the scrolling into button clicks */
    if (ev->delta_discrete) {
        seat.scroll_acc[axis] += ev->delta_discrete;
        clicks = (gint)(seat.scroll_acc[axis] /
                        WLR_POINTER_AXIS_DISCRETE_STEP);
        seat.scroll_acc[axis] -= clicks * WLR_POINTER_AXIS_DISCRETE_STEP;
    } else if (ev->delta == 0) {
        seat.scroll_acc[axis] = 0;
    } else {
        /* the X server makes a click for each 15 units of smooth scroll */
        seat.scroll_acc[axis] += ev->delta;
        clicks = (gint)(seat.scroll_acc[axis] / 15.0);
        seat.scroll_acc[axis] -= clicks * 15.0;
    }

    seat_pointer_pos(&e.x_root, &e.y_root);
    if (!seat.pgrab && seat.press_owner != PRESS_OWBOX)
        fill_event_target(&e, &surface, &sx, &sy, &node);
    else
        fill_event_target(&e, NULL, NULL, NULL, NULL);

    if (!seat.pgrab && seat.press_owner != PRESS_OWBOX &&
        other_surface(node, surface))
    {
        wlr_seat_pointer_notify_axis(server.seat, ev->time_msec,
                                     ev->orientation, ev->delta,
                                     ev->delta_discrete, ev->source,
                                     ev->relative_direction);
        return;
    }

    if (e.client && !e.win && !seat.pgrab) {
        /* client context, the scroll goes to the client unless a binding
           grabs the pointer */
        seat.replay_pending = TRUE;
        seat.replay_is_axis = TRUE;
        seat.replay_axis = *ev;
    }

    for (i = 0; i < ABS(clicks); ++i) {
        guint button = axis == 0 ? (clicks < 0 ? 4 : 5) :
            (clicks < 0 ? 6 : 7);
        e.state = event_state();
        e.button = button;
        e.time = ev->time_msec;
        e.type = OB_EVENT_BUTTON_PRESS;
        event_pointer(&e);
        if (!seat.replay_pending && seat.replay_is_axis)
            forwarded = TRUE;
        e.type = OB_EVENT_BUTTON_RELEASE;
        event_pointer(&e);
    }

    if (e.client && !e.win && !seat.pgrab && !forwarded) {
        /* smooth scrolling between clicks still goes to the client */
        if (seat.replay_pending) {
            seat.replay_pending = FALSE;
            wlr_seat_pointer_notify_axis(server.seat, ev->time_msec,
                                         ev->orientation, ev->delta,
                                         ev->delta_discrete, ev->source,
                                         ev->relative_direction);
        }
    }
    seat.replay_pending = FALSE;
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
    wlr_seat_pointer_notify_frame(server.seat);
}

/* ---- gestures, forwarded to the clients ---- */

static void handle_swipe_begin(struct wl_listener *l, void *data)
{
    struct wlr_pointer_swipe_begin_event *ev = data;
    notify_activity();
    wlr_pointer_gestures_v1_send_swipe_begin(server.pointer_gestures,
        server.seat, ev->time_msec, ev->fingers);
}

static void handle_swipe_update(struct wl_listener *l, void *data)
{
    struct wlr_pointer_swipe_update_event *ev = data;
    wlr_pointer_gestures_v1_send_swipe_update(server.pointer_gestures,
        server.seat, ev->time_msec, ev->dx, ev->dy);
}

static void handle_swipe_end(struct wl_listener *l, void *data)
{
    struct wlr_pointer_swipe_end_event *ev = data;
    wlr_pointer_gestures_v1_send_swipe_end(server.pointer_gestures,
        server.seat, ev->time_msec, ev->cancelled);
}

static void handle_pinch_begin(struct wl_listener *l, void *data)
{
    struct wlr_pointer_pinch_begin_event *ev = data;
    notify_activity();
    wlr_pointer_gestures_v1_send_pinch_begin(server.pointer_gestures,
        server.seat, ev->time_msec, ev->fingers);
}

static void handle_pinch_update(struct wl_listener *l, void *data)
{
    struct wlr_pointer_pinch_update_event *ev = data;
    wlr_pointer_gestures_v1_send_pinch_update(server.pointer_gestures,
        server.seat, ev->time_msec, ev->dx, ev->dy, ev->scale, ev->rotation);
}

static void handle_pinch_end(struct wl_listener *l, void *data)
{
    struct wlr_pointer_pinch_end_event *ev = data;
    wlr_pointer_gestures_v1_send_pinch_end(server.pointer_gestures,
        server.seat, ev->time_msec, ev->cancelled);
}

static void handle_hold_begin(struct wl_listener *l, void *data)
{
    struct wlr_pointer_hold_begin_event *ev = data;
    notify_activity();
    wlr_pointer_gestures_v1_send_hold_begin(server.pointer_gestures,
        server.seat, ev->time_msec, ev->fingers);
}

static void handle_hold_end(struct wl_listener *l, void *data)
{
    struct wlr_pointer_hold_end_event *ev = data;
    wlr_pointer_gestures_v1_send_hold_end(server.pointer_gestures,
        server.seat, ev->time_msec, ev->cancelled);
}

/* ---- touch ---- */

/* Touches on client surfaces go to the client, touches elsewhere (the
   decorations, menus, the root) work like the left mouse button */
static gint32 touch_emulating_id = -1;

static void touch_to_layout(struct wlr_touch *touch, double x, double y,
                            double *lx, double *ly)
{
    wlr_cursor_absolute_to_layout_coords(server.cursor, &touch->base,
                                         x, y, lx, ly);
}

static void emulate_button(guint32 time, gboolean press)
{
    struct wlr_pointer_button_event ev = {
        .time_msec = time,
        .button = BTN_LEFT,
        .state = press ? WL_POINTER_BUTTON_STATE_PRESSED :
            WL_POINTER_BUTTON_STATE_RELEASED,
    };
    handle_cursor_button(NULL, &ev);
    wlr_seat_pointer_notify_frame(server.seat);
}

static void handle_touch_down(struct wl_listener *l, void *data)
{
    struct wlr_touch_down_event *ev = data;
    double lx, ly, sx, sy;
    struct wlr_surface *surface = NULL;
    ObNode *node;
    ObFWin *fwin = NULL;

    notify_activity();
    touch_to_layout(ev->touch, ev->x, ev->y, &lx, &ly);
    node = window_at(lx, ly, &surface, &sx, &sy);
    node_client(node, &fwin);

    if (surface && !fwin && !menu_frame_visible && !seat.pgrab &&
        (!seat.locked || layers_surface_is_lock(surface)))
    {
        wlr_seat_touch_notify_down(server.seat, surface, ev->time_msec,
                                   ev->touch_id, sx, sy);
        return;
    }
    if (touch_emulating_id >= 0) return;
    touch_emulating_id = ev->touch_id;
    wlr_cursor_warp(server.cursor, NULL, lx, ly);
    pointer_update(ev->time_msec, TRUE);
    emulate_button(ev->time_msec, TRUE);
}

static void handle_touch_up(struct wl_listener *l, void *data)
{
    struct wlr_touch_up_event *ev = data;
    if (ev->touch_id == touch_emulating_id) {
        touch_emulating_id = -1;
        emulate_button(ev->time_msec, FALSE);
        return;
    }
    wlr_seat_touch_notify_up(server.seat, ev->time_msec, ev->touch_id);
}

static void handle_touch_motion(struct wl_listener *l, void *data)
{
    struct wlr_touch_motion_event *ev = data;
    double lx, ly;

    notify_activity();
    touch_to_layout(ev->touch, ev->x, ev->y, &lx, &ly);
    if (ev->touch_id == touch_emulating_id) {
        wlr_cursor_warp(server.cursor, NULL, lx, ly);
        pointer_update(ev->time_msec, TRUE);
        wlr_seat_pointer_notify_frame(server.seat);
        return;
    } else {
        struct wlr_touch_point *p =
            wlr_seat_touch_get_point(server.seat, ev->touch_id);
        struct wlr_scene_buffer *b;
        gint sx0, sy0;
        if (!p || !p->surface) return;
        b = window_scene_buffer_for_surface(p->surface);
        if (!b) return;
        wlr_scene_node_coords(&b->node, &sx0, &sy0);
        wlr_seat_touch_notify_motion(server.seat, ev->time_msec,
                                     ev->touch_id, lx - sx0, ly - sy0);
    }
}

static void handle_touch_cancel(struct wl_listener *l, void *data)
{
    struct wlr_touch_cancel_event *ev = data;
    if (ev->touch_id == touch_emulating_id) {
        touch_emulating_id = -1;
        emulate_button(ev->time_msec, FALSE);
        return;
    }
    {
        struct wlr_touch_point *p =
            wlr_seat_touch_get_point(server.seat, ev->touch_id);
        if (p && p->client)
            wlr_seat_touch_notify_cancel(server.seat, p->client);
    }
}

static void handle_touch_frame(struct wl_listener *l, void *data)
{
    wlr_seat_touch_notify_frame(server.seat);
}

/* ---- cursor image requests ---- */

static void handle_request_set_cursor(struct wl_listener *listener,
                                      void *data)
{
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (ev->seat_client != server.seat->pointer_state.focused_client)
        return;
    if (seat.pgrab || seat.press_owner == PRESS_OWBOX) return;
    seat.cursor_from_client = TRUE;
    seat.cursor_name = NULL;
    seat.xcursor_name = NULL;
    wlr_cursor_set_surface(server.cursor, ev->surface,
                           ev->hotspot_x, ev->hotspot_y);
}

static void handle_request_set_shape(struct wl_listener *listener,
                                     void *data)
{
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *ev = data;
    if (ev->seat_client != server.seat->pointer_state.focused_client)
        return;
    if (seat.pgrab || seat.press_owner == PRESS_OWBOX) return;
    seat.cursor_from_client = TRUE;
    seat.cursor_name = NULL;
    seat.xcursor_name = wlr_cursor_shape_v1_name(ev->shape);
    wlr_cursor_set_xcursor(server.cursor, server.xcursor_manager,
                           seat.xcursor_name);
}

/* ---- selections and drag and drop ---- */

static void handle_request_set_selection(struct wl_listener *listener,
                                         void *data)
{
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(server.seat, ev->source, ev->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener,
                                                 void *data)
{
    struct wlr_seat_request_set_primary_selection_event *ev = data;
    wlr_seat_set_primary_selection(server.seat, ev->source, ev->serial);
}

static void handle_request_start_drag(struct wl_listener *listener,
                                      void *data)
{
    struct wlr_seat_request_start_drag_event *ev = data;

    if (wlr_seat_validate_pointer_grab_serial(server.seat, ev->origin,
                                              ev->serial))
        wlr_seat_start_pointer_drag(server.seat, ev->drag, ev->serial);
    else
        wlr_data_source_destroy(ev->drag->source);
}

static void handle_start_drag(struct wl_listener *listener, void *data)
{
    struct wlr_drag *drag = data;
    if (drag->icon) {
        struct wlr_scene_tree *icon =
            wlr_scene_drag_icon_create(server.drag_tree, drag->icon);
        (void)icon;
    }
    /* the drag icon tree follows the cursor */
    wlr_scene_node_set_position(&server.drag_tree->node,
                                server.cursor->x, server.cursor->y);
}

/* ---- pointer constraints ---- */

static void update_constraint(void)
{
    struct wlr_surface *focused =
        server.seat->keyboard_state.focused_surface;
    struct wlr_pointer_constraint_v1 *c = NULL;

    if (focused)
        c = wlr_pointer_constraints_v1_constraint_for_surface(
            server.pointer_constraints, focused, server.seat);
    if (c == seat.active_constraint) return;
    if (seat.active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(seat.active_constraint);
    seat.active_constraint = c;
    if (c)
        wlr_pointer_constraint_v1_send_activated(c);
}

static void constraint_destroy(struct wl_listener *listener, void *data)
{
    ObConstraint *c = wl_container_of(listener, c, destroy);
    if (seat.active_constraint == c->constraint)
        seat.active_constraint = NULL;
    wl_list_remove(&c->destroy.link);
    g_free(c);
}

static void handle_new_constraint(struct wl_listener *listener, void *data)
{
    struct wlr_pointer_constraint_v1 *constraint = data;
    ObConstraint *c = g_new0(ObConstraint, 1);
    c->constraint = constraint;
    c->destroy.notify = constraint_destroy;
    wl_signal_add(&constraint->events.destroy, &c->destroy);
    update_constraint();
}

/* ---- keyboard shortcuts inhibit ---- */

static void inhibitor_destroy(struct wl_listener *listener, void *data)
{
    ObShortcutsInhibitor *i = wl_container_of(listener, i, destroy);
    seat.shortcut_inhibitors = g_list_remove(seat.shortcut_inhibitors, i);
    wl_list_remove(&i->destroy.link);
    g_free(i);
}

static void handle_new_shortcuts_inhibitor(struct wl_listener *listener,
                                           void *data)
{
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;
    ObShortcutsInhibitor *i = g_new0(ObShortcutsInhibitor, 1);
    i->inhibitor = inhibitor;
    i->destroy.notify = inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &i->destroy);
    seat.shortcut_inhibitors = g_list_prepend(seat.shortcut_inhibitors, i);
    update_shortcuts_inhibit();
}

/* ---- idle inhibit ---- */

typedef struct {
    struct wlr_idle_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
} ObIdleInhibitor;

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
    ObIdleInhibitor *i = wl_container_of(listener, i, destroy);
    seat.idle_inhibitors = g_list_remove(seat.idle_inhibitors, i);
    wl_list_remove(&i->destroy.link);
    g_free(i);
    seat_update_idle_inhibit();
}

static void handle_new_idle_inhibitor(struct wl_listener *listener,
                                      void *data)
{
    struct wlr_idle_inhibitor_v1 *inhibitor = data;
    ObIdleInhibitor *i = g_new0(ObIdleInhibitor, 1);
    i->inhibitor = inhibitor;
    i->destroy.notify = idle_inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &i->destroy);
    seat.idle_inhibitors = g_list_prepend(seat.idle_inhibitors, i);
    seat_update_idle_inhibit();
}

void seat_update_idle_inhibit(void)
{
    GList *it;
    gboolean inhibit = FALSE;

    for (it = seat.idle_inhibitors; it && !inhibit; it = g_list_next(it)) {
        ObIdleInhibitor *i = it->data;
        ObClient *c = client_from_surface(i->inhibitor->surface);
        /* inhibitors only work while their window can be seen */
        if (!c || (c->frame && c->frame->visible))
            inhibit = TRUE;
    }
    wlr_idle_notifier_v1_set_inhibited(server.idle_notifier, inhibit);
}

/* ---- session lock ---- */

void seat_set_locked(gboolean locked)
{
    seat.locked = locked;
    if (locked) {
        wlr_seat_keyboard_notify_clear_focus(server.seat);
        wlr_seat_pointer_notify_clear_focus(server.seat);
        seat.override_focus = NULL;
    } else {
        ObClient *c = seat.focus;
        seat.override_focus = NULL;
        if (c && c->managed)
            send_keyboard_enter(c->impl->surface(c));
        else
            send_keyboard_enter(NULL);
        seat_rebase_pointer();
    }
}

gboolean seat_is_locked(void)
{
    return seat.locked;
}

/* the lock screen gets the keyboard, see lock.c */

/* ---- setup ---- */

#define LISTEN(sig, l, f) do { (l).notify = (f); wl_signal_add((sig), &(l)); } while (0)

void seat_init(void)
{
    wl_list_init(&seat.keyboards);
    wl_list_init(&seat.pointers);
    seat.used_keys = g_hash_table_new(g_direct_hash, g_direct_equal);

    server.seat = wlr_seat_create(server.display, "seat0");
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.xcursor_manager =
        wlr_xcursor_manager_create(config_cursor_theme, config_cursor_size);
    export_cursor_theme();

    LISTEN(&server.backend->events.new_input, seat.new_input,
           handle_new_input);
    LISTEN(&server.cursor->events.motion, seat.cursor_motion,
           handle_cursor_motion);
    LISTEN(&server.cursor->events.motion_absolute,
           seat.cursor_motion_absolute, handle_cursor_motion_absolute);
    LISTEN(&server.cursor->events.button, seat.cursor_button,
           handle_cursor_button);
    LISTEN(&server.cursor->events.axis, seat.cursor_axis, handle_cursor_axis);
    LISTEN(&server.cursor->events.frame, seat.cursor_frame,
           handle_cursor_frame);
    LISTEN(&server.cursor->events.swipe_begin, seat.swipe_begin,
           handle_swipe_begin);
    LISTEN(&server.cursor->events.swipe_update, seat.swipe_update,
           handle_swipe_update);
    LISTEN(&server.cursor->events.swipe_end, seat.swipe_end, handle_swipe_end);
    LISTEN(&server.cursor->events.pinch_begin, seat.pinch_begin,
           handle_pinch_begin);
    LISTEN(&server.cursor->events.pinch_update, seat.pinch_update,
           handle_pinch_update);
    LISTEN(&server.cursor->events.pinch_end, seat.pinch_end, handle_pinch_end);
    LISTEN(&server.cursor->events.hold_begin, seat.hold_begin,
           handle_hold_begin);
    LISTEN(&server.cursor->events.hold_end, seat.hold_end, handle_hold_end);
    LISTEN(&server.cursor->events.touch_down, seat.touch_down,
           handle_touch_down);
    LISTEN(&server.cursor->events.touch_up, seat.touch_up, handle_touch_up);
    LISTEN(&server.cursor->events.touch_motion, seat.touch_motion,
           handle_touch_motion);
    LISTEN(&server.cursor->events.touch_cancel, seat.touch_cancel,
           handle_touch_cancel);
    LISTEN(&server.cursor->events.touch_frame, seat.touch_frame,
           handle_touch_frame);

    LISTEN(&server.seat->events.request_set_cursor, seat.request_set_cursor,
           handle_request_set_cursor);
    LISTEN(&server.seat->events.request_set_selection,
           seat.request_set_selection, handle_request_set_selection);
    LISTEN(&server.seat->events.request_set_primary_selection,
           seat.request_set_primary_selection,
           handle_request_set_primary_selection);
    LISTEN(&server.seat->events.request_start_drag, seat.request_start_drag,
           handle_request_start_drag);
    LISTEN(&server.seat->events.start_drag, seat.start_drag,
           handle_start_drag);

    server.cursor_shape = wlr_cursor_shape_manager_v1_create(server.display,
                                                             1);
    LISTEN(&server.cursor_shape->events.request_set_shape,
           seat.request_set_shape, handle_request_set_shape);

    server.relative_pointer =
        wlr_relative_pointer_manager_v1_create(server.display);
    server.pointer_gestures = wlr_pointer_gestures_v1_create(server.display);
    server.pointer_constraints =
        wlr_pointer_constraints_v1_create(server.display);
    LISTEN(&server.pointer_constraints->events.new_constraint,
           seat.new_constraint, handle_new_constraint);

    server.shortcuts_inhibit =
        wlr_keyboard_shortcuts_inhibit_v1_create(server.display);
    LISTEN(&server.shortcuts_inhibit->events.new_inhibitor,
           seat.new_shortcuts_inhibitor, handle_new_shortcuts_inhibitor);

    server.idle_notifier = wlr_idle_notifier_v1_create(server.display);
    server.idle_inhibit = wlr_idle_inhibit_v1_create(server.display);
    LISTEN(&server.idle_inhibit->events.new_inhibitor,
           seat.new_idle_inhibitor, handle_new_idle_inhibitor);

    server.virtual_keyboard =
        wlr_virtual_keyboard_manager_v1_create(server.display);
    LISTEN(&server.virtual_keyboard->events.new_virtual_keyboard,
           seat.new_virtual_keyboard, handle_new_virtual_keyboard);
    server.virtual_pointer =
        wlr_virtual_pointer_manager_v1_create(server.display);
    LISTEN(&server.virtual_pointer->events.new_virtual_pointer,
           seat.new_virtual_pointer, handle_new_virtual_pointer);

    seat_update_capabilities();
}

void seat_finish(void)
{
    wl_list_remove(&seat.new_input.link);
    wl_list_remove(&seat.cursor_motion.link);
    wl_list_remove(&seat.cursor_motion_absolute.link);
    wl_list_remove(&seat.cursor_button.link);
    wl_list_remove(&seat.cursor_axis.link);
    wl_list_remove(&seat.cursor_frame.link);
    wl_list_remove(&seat.swipe_begin.link);
    wl_list_remove(&seat.swipe_update.link);
    wl_list_remove(&seat.swipe_end.link);
    wl_list_remove(&seat.pinch_begin.link);
    wl_list_remove(&seat.pinch_update.link);
    wl_list_remove(&seat.pinch_end.link);
    wl_list_remove(&seat.hold_begin.link);
    wl_list_remove(&seat.hold_end.link);
    wl_list_remove(&seat.touch_down.link);
    wl_list_remove(&seat.touch_up.link);
    wl_list_remove(&seat.touch_motion.link);
    wl_list_remove(&seat.touch_cancel.link);
    wl_list_remove(&seat.touch_frame.link);
    wl_list_remove(&seat.request_set_cursor.link);
    wl_list_remove(&seat.request_set_selection.link);
    wl_list_remove(&seat.request_set_primary_selection.link);
    wl_list_remove(&seat.request_start_drag.link);
    wl_list_remove(&seat.start_drag.link);
    wl_list_remove(&seat.request_set_shape.link);
    wl_list_remove(&seat.new_constraint.link);
    wl_list_remove(&seat.new_shortcuts_inhibitor.link);
    wl_list_remove(&seat.new_idle_inhibitor.link);
    wl_list_remove(&seat.new_virtual_keyboard.link);
    wl_list_remove(&seat.new_virtual_pointer.link);
    g_hash_table_destroy(seat.used_keys);
    wlr_xcursor_manager_destroy(server.xcursor_manager);
    wlr_cursor_destroy(server.cursor);
}

