/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ime.c for owbox
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

/* The relay between applications and the input method, as in sway and
   labwc */

#include "ime.h"
#include "server.h"
#include "window.h"
#include "debug.h"

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

/*! An application's text input */
typedef struct {
    struct wlr_text_input_v3 *input;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener commit;
    struct wl_listener destroy;
} ObTextInput;

/*! The input method's popup (the candidates) */
typedef struct {
    struct wlr_input_popup_surface_v2 *popup;
    struct wlr_scene_tree *tree;
    struct wl_listener commit;
    struct wl_listener destroy;
} ObImePopup;

static struct wlr_text_input_manager_v3 *text_input_manager;
static struct wlr_input_method_manager_v2 *input_method_manager;

static GList *text_inputs = NULL; /* ObTextInput* */
static GList *popups = NULL;      /* ObImePopup* */
/*! The input method, there is one at a time */
static struct wlr_input_method_v2 *input_method = NULL;
/*! The text input the input method types into */
static ObTextInput *active = NULL;
/*! The surface with the keyboard focus */
static struct wlr_surface *focus = NULL;

static struct wl_listener new_text_input;
static struct wl_listener new_input_method;
static struct wl_listener keyboard_focus;
static struct wl_listener focus_destroy;
static struct wl_listener im_commit;
static struct wl_listener im_grab_keyboard;
static struct wl_listener im_grab_destroy;
static struct wl_listener im_new_popup;
static struct wl_listener im_destroy;

static gboolean same_client(struct wl_resource *a, struct wl_resource *b)
{
    return wl_resource_get_client(a) == wl_resource_get_client(b);
}

/* ---- keys ---- */

/* the input method's own virtual keyboard: its keys go to the application,
   not back to it */
static gboolean from_input_method(struct wlr_keyboard *keyboard)
{
    struct wlr_virtual_keyboard_v1 *vkb;

    if (!keyboard || !input_method) return FALSE;
    vkb = wlr_input_device_get_virtual_keyboard(&keyboard->base);
    return vkb && same_client(vkb->resource, input_method->resource);
}

static struct wlr_input_method_keyboard_grab_v2* grab_for(
    struct wlr_keyboard *keyboard)
{
    if (!input_method || !input_method->keyboard_grab ||
        from_input_method(keyboard))
        return NULL;
    return input_method->keyboard_grab;
}

gboolean ime_key(struct wlr_keyboard *keyboard,
                 struct wlr_keyboard_key_event *ev)
{
    struct wlr_input_method_keyboard_grab_v2 *grab = grab_for(keyboard);

    if (!grab) return FALSE;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(grab, ev->time_msec,
                                               ev->keycode, ev->state);
    return TRUE;
}

gboolean ime_modifiers(struct wlr_keyboard *keyboard)
{
    struct wlr_input_method_keyboard_grab_v2 *grab = grab_for(keyboard);

    if (!grab) return FALSE;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(grab,
                                                     &keyboard->modifiers);
    return TRUE;
}

/* ---- the relay ---- */

/* The text input that is enabled on the focused surface */
static ObTextInput* find_active(void)
{
    GList *it;

    if (!input_method) return NULL;
    for (it = text_inputs; it; it = g_list_next(it)) {
        ObTextInput *t = it->data;
        if (t->input->focused_surface && t->input->current_enabled)
            return t;
    }
    return NULL;
}

/* Activate or deactivate the input method when the active text input
   changes */
static void update_active(void)
{
    ObTextInput *t = find_active();

    if (input_method && t != active) {
        if (t)
            wlr_input_method_v2_send_activate(input_method);
        else
            wlr_input_method_v2_send_deactivate(input_method);
        wlr_input_method_v2_send_done(input_method);
    }
    active = t;
}

/* The text inputs of the focused surface's client enter it, the others
   leave, while there is an input method */
static void update_focus(void)
{
    GList *it;

    for (it = text_inputs; it; it = g_list_next(it)) {
        ObTextInput *t = it->data;
        struct wlr_surface *s = NULL;

        if (input_method && focus &&
            same_client(t->input->resource, focus->resource))
            s = focus;
        if (t->input->focused_surface == s) continue;
        if (t->input->focused_surface)
            wlr_text_input_v3_send_leave(t->input);
        if (s)
            wlr_text_input_v3_send_enter(t->input, s);
    }
}

static void set_focus(struct wlr_surface *surface)
{
    if (surface == focus) return;
    if (focus)
        wl_list_remove(&focus_destroy.link);
    focus = surface;
    if (focus)
        wl_signal_add(&focus->events.destroy, &focus_destroy);
    update_focus();
    update_active();
}

static void handle_focus_destroy(struct wl_listener *l, void *data)
{
    set_focus(NULL);
}

static void handle_keyboard_focus(struct wl_listener *l, void *data)
{
    struct wlr_seat_keyboard_focus_change_event *ev = data;
    set_focus(ev->new_surface);
}

/* ---- the popup ---- */

/* Below the text cursor, or above it if there's no room, inside the
   monitor */
static void popup_place(ObImePopup *p)
{
    struct wlr_text_input_v3 *ti;
    struct wlr_scene_buffer *b;
    struct wlr_box cursor = {0}, area, box;
    struct wlr_output *o;
    struct wlr_xdg_positioner_rules rules = {0};
    double cx, cy;
    gint lx = 0, ly = 0;

    if (!active || !focus || !p->popup->surface->mapped) return;
    ti = active->input;

    /* the text cursor in layout coordinates */
    if ((b = window_scene_buffer_for_surface(focus)))
        wlr_scene_node_coords(&b->node, &lx, &ly);
    if (ti->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)
        cursor = ti->current.cursor_rectangle;
    cursor.x += lx;
    cursor.y += ly;

    wlr_output_layout_closest_point(server.output_layout, NULL,
                                    cursor.x, cursor.y, &cx, &cy);
    if (!(o = wlr_output_layout_output_at(server.output_layout, cx, cy)))
        return;
    wlr_output_layout_get_box(server.output_layout, o, &area);

    rules.anchor_rect = cursor;
    rules.anchor = XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
    rules.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
    rules.constraint_adjustment =
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X;
    rules.size.width = p->popup->surface->current.width;
    rules.size.height = p->popup->surface->current.height;
    wlr_xdg_positioner_rules_get_geometry(&rules, &box);
    wlr_xdg_positioner_rules_unconstrain_box(&rules, &area, &box);

    wlr_scene_node_set_position(&p->tree->node, box.x, box.y);
    wlr_input_popup_surface_v2_send_text_input_rectangle(
        p->popup, &(struct wlr_box){ cursor.x - box.x, cursor.y - box.y,
                                     cursor.width, cursor.height });
}

static void popups_place(void)
{
    GList *it;
    for (it = popups; it; it = g_list_next(it))
        popup_place(it->data);
}

static void popup_commit(struct wl_listener *l, void *data)
{
    ObImePopup *p = wl_container_of(l, p, commit);
    popup_place(p);
}

static void popup_destroy(struct wl_listener *l, void *data)
{
    ObImePopup *p = wl_container_of(l, p, destroy);

    wlr_scene_node_destroy(&p->tree->node);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    popups = g_list_remove(popups, p);
    g_free(p);
}

static void handle_im_new_popup(struct wl_listener *l, void *data)
{
    ObImePopup *p = g_new0(ObImePopup, 1);

    p->popup = data;
    p->tree = wlr_scene_subsurface_tree_create(server.ime_tree,
                                               p->popup->surface);
    p->commit.notify = popup_commit;
    wl_signal_add(&p->popup->surface->events.commit, &p->commit);
    p->destroy.notify = popup_destroy;
    wl_signal_add(&p->popup->events.destroy, &p->destroy);
    popups = g_list_prepend(popups, p);
    popup_place(p);
}

/* ---- the input method ---- */

/* what the input method typed goes to the application */
static void handle_im_commit(struct wl_listener *l, void *data)
{
    struct wlr_input_method_v2_state *st = &input_method->current;
    struct wlr_text_input_v3 *ti;

    if (!active) return;
    ti = active->input;
    if (st->preedit.text)
        wlr_text_input_v3_send_preedit_string(ti, st->preedit.text,
                                              st->preedit.cursor_begin,
                                              st->preedit.cursor_end);
    if (st->commit_text)
        wlr_text_input_v3_send_commit_string(ti, st->commit_text);
    if (st->delete.before_length || st->delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(
            ti, st->delete.before_length, st->delete.after_length);
    wlr_text_input_v3_send_done(ti);
}

static void handle_im_grab_destroy(struct wl_listener *l, void *data)
{
    /* the signal has no data, the input method still has the grab */
    struct wlr_input_method_keyboard_grab_v2 *grab =
        input_method->keyboard_grab;

    wl_list_remove(&im_grab_destroy.link);
    /* the application gets the modifiers that are held now */
    if (grab->keyboard)
        wlr_seat_keyboard_notify_modifiers(server.seat,
                                           &grab->keyboard->modifiers);
}

static void handle_im_grab_keyboard(struct wl_listener *l, void *data)
{
    struct wlr_input_method_keyboard_grab_v2 *grab = data;
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server.seat);

    /* the modifiers held now */
    if (kb && !from_input_method(kb))
        wlr_input_method_keyboard_grab_v2_set_keyboard(grab, kb);
    im_grab_destroy.notify = handle_im_grab_destroy;
    wl_signal_add(&grab->events.destroy, &im_grab_destroy);
}

static void handle_im_destroy(struct wl_listener *l, void *data)
{
    wl_list_remove(&im_commit.link);
    wl_list_remove(&im_grab_keyboard.link);
    wl_list_remove(&im_new_popup.link);
    wl_list_remove(&im_destroy.link);
    input_method = NULL;
    ob_debug("The input method is gone");
    update_focus();
    update_active();
}

static void handle_new_input_method(struct wl_listener *l, void *data)
{
    struct wlr_input_method_v2 *im = data;

    if (im->seat != server.seat) return;
    if (input_method) {
        /* one at a time */
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    ob_debug("An input method started");
    input_method = im;
    im_commit.notify = handle_im_commit;
    wl_signal_add(&im->events.commit, &im_commit);
    im_grab_keyboard.notify = handle_im_grab_keyboard;
    wl_signal_add(&im->events.grab_keyboard, &im_grab_keyboard);
    im_new_popup.notify = handle_im_new_popup;
    wl_signal_add(&im->events.new_popup_surface, &im_new_popup);
    im_destroy.notify = handle_im_destroy;
    wl_signal_add(&im->events.destroy, &im_destroy);
    update_focus();
    update_active();
}

/* ---- text inputs ---- */

/* the application's state (its text around the cursor, what kind of text
   it wants) goes to the input method */
static void send_state(void)
{
    struct wlr_text_input_v3 *ti = active->input;

    if (ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(
            input_method, ti->current.surrounding.text,
            ti->current.surrounding.cursor, ti->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(
        input_method, ti->current.text_change_cause);
    if (ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(
            input_method, ti->current.content_type.hint,
            ti->current.content_type.purpose);
    wlr_input_method_v2_send_done(input_method);
}

static void text_input_enable(struct wl_listener *l, void *data)
{
    ObTextInput *t = wl_container_of(l, t, enable);

    update_active();
    if (active == t) {
        popups_place();
        send_state();
    }
}

static void text_input_disable(struct wl_listener *l, void *data)
{
    update_active();
}

static void text_input_commit(struct wl_listener *l, void *data)
{
    ObTextInput *t = wl_container_of(l, t, commit);

    if (active == t) {
        popups_place();
        send_state();
    }
}

static void text_input_destroy(struct wl_listener *l, void *data)
{
    ObTextInput *t = wl_container_of(l, t, destroy);

    wl_list_remove(&t->enable.link);
    wl_list_remove(&t->disable.link);
    wl_list_remove(&t->commit.link);
    wl_list_remove(&t->destroy.link);
    text_inputs = g_list_remove(text_inputs, t);
    if (active == t) active = NULL;
    g_free(t);
    update_active();
}

static void handle_new_text_input(struct wl_listener *l, void *data)
{
    struct wlr_text_input_v3 *input = data;
    ObTextInput *t;

    if (input->seat != server.seat) return;
    t = g_new0(ObTextInput, 1);
    t->input = input;
    t->enable.notify = text_input_enable;
    wl_signal_add(&input->events.enable, &t->enable);
    t->disable.notify = text_input_disable;
    wl_signal_add(&input->events.disable, &t->disable);
    t->commit.notify = text_input_commit;
    wl_signal_add(&input->events.commit, &t->commit);
    t->destroy.notify = text_input_destroy;
    wl_signal_add(&input->events.destroy, &t->destroy);
    text_inputs = g_list_prepend(text_inputs, t);
    update_focus();
}

/* ---- setup ---- */

void ime_init(void)
{
    text_input_manager = wlr_text_input_manager_v3_create(server.display);
    input_method_manager = wlr_input_method_manager_v2_create(server.display);

    new_text_input.notify = handle_new_text_input;
    wl_signal_add(&text_input_manager->events.new_text_input,
                  &new_text_input);
    new_input_method.notify = handle_new_input_method;
    wl_signal_add(&input_method_manager->events.new_input_method,
                  &new_input_method);
    keyboard_focus.notify = handle_keyboard_focus;
    wl_signal_add(&server.seat->keyboard_state.events.focus_change,
                  &keyboard_focus);
    focus_destroy.notify = handle_focus_destroy;
}

void ime_finish(void)
{
    if (focus)
        wl_list_remove(&focus_destroy.link);
    focus = NULL;
    wl_list_remove(&new_text_input.link);
    wl_list_remove(&new_input_method.link);
    wl_list_remove(&keyboard_focus.link);
}
