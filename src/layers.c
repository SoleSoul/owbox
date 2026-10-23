/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   layers.c for owbox
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

#include "layers.h"
#include "server.h"
#include "output.h"
#include "screen.h"
#include "seat.h"
#include "openbox.h"
#include "debug.h"
#include "config.h"
#include "panels.h"
#include "xdg.h"

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

static GList *surfaces = NULL; /* ObLayerSurface* */
static GList *struts = NULL;   /* StrutPartial* */
/*! The layer surface that holds the keyboard (exclusive interactivity) */
static ObLayerSurface *exclusive = NULL;

static struct wl_listener new_surface;

/* ---- arranging ---- */

static void arrange_layer(ObOutput *o, enum zwlr_layer_shell_v1_layer layer,
                          const struct wlr_box *full,
                          struct wlr_box *usable, gboolean exclusive_only)
{
    GList *it;

    for (it = surfaces; it; it = g_list_next(it)) {
        ObLayerSurface *s = it->data;
        struct wlr_layer_surface_v1 *ls = s->layer_surface;

        if (s->output != o || !ls->initialized) continue;
        if (ls->current.layer != layer) continue;
        if ((ls->current.exclusive_zone > 0) != exclusive_only) continue;

        if (config_panels_autohide && layers_is_panel(s)) {
            /* owbox: hidden panels don't take space, they show over the
               windows */
            struct wlr_box scratch = *usable;
            wlr_scene_layer_surface_v1_configure(s->scene, full, &scratch);
        } else
            wlr_scene_layer_surface_v1_configure(s->scene, full, usable);
        /* popups go along with it */
        wlr_scene_node_set_position(&s->popup_tree->node,
                                    s->tree->node.x, s->tree->node.y);
    }
}

static void arrange_output(ObOutput *o)
{
    struct wlr_box full, usable;
    gint layer;
    StrutPartial *strut;
    const Rect *all;

    if (!o->in_layout) return;

    wlr_output_layout_get_box(server.output_layout, o->wlr_output, &full);
    usable = full;

    /* the ones reserving space first, from the top layer down, then the
       rest in the space that is left */
    for (layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
         layer >= ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; --layer)
        arrange_layer(o, layer, &full, &usable, TRUE);
    for (layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
         layer >= ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; --layer)
        arrange_layer(o, layer, &full, &usable, FALSE);

    /* the space taken from the output becomes a strut, measured from the
       edges of the whole screen like X struts are */
    if (usable.x == full.x && usable.y == full.y &&
        usable.width == full.width && usable.height == full.height)
        return;
    all = screen_physical_area_all_monitors();
    strut = g_slice_new0(StrutPartial);
    if (usable.y > full.y) {
        strut->top = usable.y - all->y;
        strut->top_start = full.x;
        strut->top_end = full.x + full.width - 1;
    }
    if (usable.y + usable.height < full.y + full.height) {
        strut->bottom = all->y + all->height - (usable.y + usable.height);
        strut->bottom_start = full.x;
        strut->bottom_end = full.x + full.width - 1;
    }
    if (usable.x > full.x) {
        strut->left = usable.x - all->x;
        strut->left_start = full.y;
        strut->left_end = full.y + full.height - 1;
    }
    if (usable.x + usable.width < full.x + full.width) {
        strut->right = all->x + all->width - (usable.x + usable.width);
        strut->right_start = full.y;
        strut->right_end = full.y + full.height - 1;
    }
    struts = g_list_prepend(struts, strut);
}

static void free_struts(void)
{
    GList *it;
    for (it = struts; it; it = g_list_next(it))
        g_slice_free(StrutPartial, it->data);
    g_list_free(struts);
    struts = NULL;
}

/* ---- panels (owbox) ---- */

gboolean layers_is_panel(ObLayerSurface *s)
{
    const guint32 horz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const guint32 vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    struct wlr_layer_surface_v1 *ls = s->layer_surface;
    guint32 a;

    if (!ls->initialized) return FALSE;
    if (ls->current.layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP &&
        ls->current.layer != ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM)
        return FALSE;
    /* a bar along one edge (a notification is in a corner, a launcher in
       the middle) */
    a = ls->current.anchor;
    return ((a & horz) == horz && (a & vert) && (a & vert) != vert) ||
        ((a & vert) == vert && (a & horz) && (a & horz) != horz);
}

/* where a panel goes: above the dim while the panels are shown, its layer
   otherwise, and hidden then if they hide */
static void place_panel(ObLayerSurface *s)
{
    gboolean shown = panels_shown();
    struct wlr_scene_tree *parent;

    if (!layers_is_panel(s)) return;
    parent = shown ? server.panels_tree :
        server.layer_tree[s->layer_surface->current.layer];
    if (s->tree->node.parent != parent)
        wlr_scene_node_reparent(&s->tree->node, parent);
    wlr_scene_node_set_enabled(&s->tree->node,
                               shown || !config_panels_autohide);
    wlr_scene_node_set_enabled(&s->popup_tree->node,
                               shown || !config_panels_autohide);
}

void layers_place_panels(void)
{
    GList *it;
    for (it = surfaces; it; it = g_list_next(it))
        place_panel(it->data);
}

void layers_for_each_panel(void (*func)(ObLayerSurface *s, gpointer data),
                           gpointer data)
{
    GList *it;
    for (it = surfaces; it; it = g_list_next(it))
        if (((ObLayerSurface*)it->data)->mapped &&
            layers_is_panel(it->data))
            func(it->data, data);
}

ObLayerSurface* layers_surface_owner(struct wlr_surface *surface)
{
    /* through the popups (a tray menu) to the layer surface */
    while (surface) {
        struct wlr_surface *root = wlr_surface_get_root_surface(surface);
        struct wlr_layer_surface_v1 *ls;
        struct wlr_xdg_popup *popup;

        if ((ls = wlr_layer_surface_v1_try_from_wlr_surface(root)))
            return ls->data;
        if (!(popup = wlr_xdg_popup_try_from_wlr_surface(root)))
            return NULL;
        surface = popup->parent;
    }
    return NULL;
}

gboolean layers_surface_wants_keyboard(ObLayerSurface *s)
{
    return s->layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
}

void layers_close(ObLayerSurface *s)
{
    wlr_layer_surface_v1_destroy(s->layer_surface);
}

ObLayerSurface* layers_keyboard_surface(void)
{
    struct wlr_surface *focus = server.seat->keyboard_state.focused_surface;
    return focus ? layers_surface_owner(focus) : NULL;
}

void layers_arrange_all(void)
{
    ObOutput *o;

    free_struts();
    wl_list_for_each(o, &server.outputs, link)
        arrange_output(o);

    if (ob_state() == OB_STATE_RUNNING)
        screen_update_areas();
}

GList* layers_struts(void)
{
    return struts;
}

/* ---- keyboard ---- */

static void update_keyboard(void)
{
    GList *it;
    ObLayerSurface *want = NULL;

    /* the topmost surface asking for the keyboard exclusively gets it */
    for (it = surfaces; it; it = g_list_next(it)) {
        ObLayerSurface *s = it->data;
        struct wlr_layer_surface_v1 *ls = s->layer_surface;
        if (!s->mapped) continue;
        if (ls->current.keyboard_interactive !=
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
            continue;
        if (ls->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP) continue;
        if (!want || ls->current.layer > want->layer_surface->current.layer)
            want = s;
    }

    if (want == exclusive) return;
    exclusive = want;
    seat_focus_surface(want ? want->layer_surface->surface : NULL);
}

void layers_surface_clicked(struct wlr_surface *surface)
{
    struct wlr_layer_surface_v1 *ls;

    if (exclusive || !surface) return;
    ls = wlr_layer_surface_v1_try_from_wlr_surface(
        wlr_surface_get_root_surface(surface));
    if (ls && ls->current.keyboard_interactive ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND)
        seat_focus_surface(ls->surface);
}

gboolean layers_surface_is_root(ObWindow *win)
{
    ObLayerSurface *s = (ObLayerSurface*)win;
    return s->layer_surface->current.layer ==
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
}

/* ---- layer surfaces ---- */

static void handle_map(struct wl_listener *l, void *data)
{
    ObLayerSurface *s = wl_container_of(l, s, map);
    s->mapped = TRUE;
    place_panel(s);
    panels_layer_mapped(s);
    layers_arrange_all();
    update_keyboard();
    seat_rebase_pointer();
}

static void handle_unmap(struct wl_listener *l, void *data)
{
    ObLayerSurface *s = wl_container_of(l, s, unmap);
    s->mapped = FALSE;
    panels_layer_gone(s);
    if (exclusive == s) {
        exclusive = NULL;
        seat_focus_surface(NULL);
    } else
        seat_surface_gone(s->layer_surface->surface);
    layers_arrange_all();
    update_keyboard();
    seat_rebase_pointer();
}

static void handle_commit(struct wl_listener *l, void *data)
{
    ObLayerSurface *s = wl_container_of(l, s, commit);
    struct wlr_layer_surface_v1 *ls = s->layer_surface;
    struct wlr_scene_tree *parent;

    if (!ls->initialized) return;

    if (ls->current.committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
        parent = server.layer_tree[ls->current.layer];
        wlr_scene_node_reparent(&s->tree->node, parent);
    }
    if (ls->current.committed &
        (WLR_LAYER_SURFACE_V1_STATE_LAYER | WLR_LAYER_SURFACE_V1_STATE_ANCHOR))
        place_panel(s);

    if (ls->current.committed || ls->initial_commit) {
        layers_arrange_all();
        update_keyboard();
    }
}

static void handle_destroy(struct wl_listener *l, void *data)
{
    ObLayerSurface *s = wl_container_of(l, s, destroy);

    panels_layer_gone(s);
    surfaces = g_list_remove(surfaces, s);
    if (exclusive == s) {
        exclusive = NULL;
        seat_focus_surface(NULL);
    }
    wl_list_remove(&s->map.link);
    wl_list_remove(&s->unmap.link);
    wl_list_remove(&s->commit.link);
    wl_list_remove(&s->destroy.link);
    wl_list_remove(&s->new_popup.link);
    wlr_scene_node_destroy(&s->popup_tree->node);
    g_free(s);

    layers_arrange_all();
}

static void handle_new_popup(struct wl_listener *l, void *data)
{
    ObLayerSurface *s = wl_container_of(l, s, new_popup);
    struct wlr_xdg_popup *popup = data;

    /* the popup has had no parent until now (get_popup), so xdg.c left it
       alone (a tray icon's menu) */
    if (!popup->base->data)
        xdg_popup_add(popup, s->popup_tree);
}

static void handle_new_surface(struct wl_listener *l, void *data)
{
    struct wlr_layer_surface_v1 *ls = data;
    ObLayerSurface *s;
    ObOutput *o;

    if (!ls->output) {
        /* put it on the monitor the user is on, where new windows go too */
        guint m = screen_monitor_current();
        o = m < output_count() ? output_nth(m) : NULL;
        if (!o && output_count()) o = output_nth(0);
        if (!o) {
            wlr_layer_surface_v1_destroy(ls);
            return;
        }
        ls->output = o->wlr_output;
    }
    o = output_from_wlr(ls->output);

    s = g_new0(ObLayerSurface, 1);
    s->obwin.type = OB_WINDOW_CLASS_LAYER;
    s->node.type = OB_NODE_WINDOW;
    s->node.owner = &s->obwin;
    s->layer_surface = ls;
    s->output = o;
    s->scene = wlr_scene_layer_surface_v1_create(
        server.layer_tree[ls->pending.layer], ls);
    s->tree = s->scene->tree;
    ob_node_attach(&s->node, &s->tree->node);
    s->popup_tree = wlr_scene_tree_create(server.layer_popup_tree);
    ls->data = s;

    s->map.notify = handle_map;
    wl_signal_add(&ls->surface->events.map, &s->map);
    s->unmap.notify = handle_unmap;
    wl_signal_add(&ls->surface->events.unmap, &s->unmap);
    s->commit.notify = handle_commit;
    wl_signal_add(&ls->surface->events.commit, &s->commit);
    s->destroy.notify = handle_destroy;
    wl_signal_add(&ls->events.destroy, &s->destroy);
    s->new_popup.notify = handle_new_popup;
    wl_signal_add(&ls->events.new_popup, &s->new_popup);

    surfaces = g_list_append(surfaces, s);
}

void layers_output_destroyed(ObOutput *output)
{
    GList *it, *next;
    for (it = surfaces; it; it = next) {
        ObLayerSurface *s = it->data;
        next = g_list_next(it);
        if (s->output == output) {
            s->output = NULL;
            wlr_layer_surface_v1_destroy(s->layer_surface);
        }
    }
}

struct wlr_scene_tree* layers_popup_parent_tree(struct wlr_surface *parent)
{
    struct wlr_layer_surface_v1 *ls =
        wlr_layer_surface_v1_try_from_wlr_surface(parent);
    if (ls && ls->data)
        return ((ObLayerSurface*)ls->data)->popup_tree;
    return NULL;
}

void layers_popup_unconstrain(struct wlr_xdg_popup *popup)
{
    struct wlr_surface *root = popup->parent;
    struct wlr_layer_surface_v1 *ls;
    ObLayerSurface *s;
    struct wlr_box obox, box;

    while (root) {
        struct wlr_xdg_surface *x = wlr_xdg_surface_try_from_wlr_surface(root);
        if (!x || x->role != WLR_XDG_SURFACE_ROLE_POPUP) break;
        root = x->popup->parent;
    }
    ls = root ? wlr_layer_surface_v1_try_from_wlr_surface(root) : NULL;
    if (!ls || !(s = ls->data) || !s->output) return;

    wlr_output_layout_get_box(server.output_layout, s->output->wlr_output,
                              &obox);
    box.x = obox.x - s->tree->node.x;
    box.y = obox.y - s->tree->node.y;
    box.width = obox.width;
    box.height = obox.height;
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

/* ---- session lock ---- */

typedef struct {
    struct wlr_session_lock_surface_v1 *surface;
    struct wlr_scene_tree *tree;
    struct wl_listener map;
    struct wl_listener destroy;
} ObLockSurface;

static struct wlr_session_lock_v1 *active_lock = NULL;
/*! Blank the screen under the lock surfaces, and when the locker dies */
static struct wlr_scene_rect *lock_blank = NULL;
static GList *lock_surfaces = NULL;
static struct wl_listener new_lock;
static struct wl_listener lock_new_surface;
static struct wl_listener lock_unlock;
static struct wl_listener lock_destroy;

gboolean layers_surface_is_lock(struct wlr_surface *surface)
{
    return surface &&
        wlr_session_lock_surface_v1_try_from_wlr_surface(
            wlr_surface_get_root_surface(surface)) != NULL;
}

static void lock_surface_map(struct wl_listener *l, void *data)
{
    ObLockSurface *s = wl_container_of(l, s, map);
    /* give the keyboard to the lock screen */
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server.seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(server.seat, s->surface->surface,
                                       kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    seat_rebase_pointer();
}

static void lock_surface_destroy(struct wl_listener *l, void *data)
{
    ObLockSurface *s = wl_container_of(l, s, destroy);
    lock_surfaces = g_list_remove(lock_surfaces, s);
    wl_list_remove(&s->map.link);
    wl_list_remove(&s->destroy.link);
    g_free(s);
}

static void handle_lock_new_surface(struct wl_listener *l, void *data)
{
    struct wlr_session_lock_surface_v1 *ls = data;
    ObLockSurface *s = g_new0(ObLockSurface, 1);
    struct wlr_box box;

    s->surface = ls;
    s->tree = wlr_scene_subsurface_tree_create(server.lock_tree,
                                               ls->surface);
    wlr_output_layout_get_box(server.output_layout, ls->output, &box);
    wlr_scene_node_set_position(&s->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(ls, box.width, box.height);

    s->map.notify = lock_surface_map;
    wl_signal_add(&ls->surface->events.map, &s->map);
    s->destroy.notify = lock_surface_destroy;
    wl_signal_add(&ls->events.destroy, &s->destroy);
    lock_surfaces = g_list_prepend(lock_surfaces, s);
}

static void lock_stop_listening(void)
{
    wl_list_remove(&lock_new_surface.link);
    wl_list_remove(&lock_unlock.link);
    wl_list_remove(&lock_destroy.link);
    active_lock = NULL;
}

static void handle_lock_unlock(struct wl_listener *l, void *data)
{
    lock_stop_listening();
    if (lock_blank) {
        wlr_scene_node_destroy(&lock_blank->node);
        lock_blank = NULL;
    }
    seat_set_locked(FALSE);
}

static void handle_lock_destroy(struct wl_listener *l, void *data)
{
    /* the locker went away without unlocking, stay locked (blank) until a
       new locker unlocks */
    lock_stop_listening();
}

static void handle_new_lock(struct wl_listener *l, void *data)
{
    struct wlr_session_lock_v1 *lock = data;
    struct wlr_box box;

    if (active_lock) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    active_lock = lock;
    lock_new_surface.notify = handle_lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &lock_new_surface);
    lock_unlock.notify = handle_lock_unlock;
    wl_signal_add(&lock->events.unlock, &lock_unlock);
    lock_destroy.notify = handle_lock_destroy;
    wl_signal_add(&lock->events.destroy, &lock_destroy);

    if (!lock_blank) {
        static const float black[4] = { 0, 0, 0, 1 };
        wlr_output_layout_get_box(server.output_layout, NULL, &box);
        lock_blank = wlr_scene_rect_create(server.lock_tree, box.width,
                                           box.height, black);
        wlr_scene_node_set_position(&lock_blank->node, box.x, box.y);
        wlr_scene_node_lower_to_bottom(&lock_blank->node);
    }
    seat_set_locked(TRUE);
    wlr_session_lock_v1_send_locked(lock);
}

/* ---- setup ---- */

void layers_init(void)
{
    server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
    new_surface.notify = handle_new_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &new_surface);

    server.session_lock = wlr_session_lock_manager_v1_create(server.display);
    new_lock.notify = handle_new_lock;
    wl_signal_add(&server.session_lock->events.new_lock, &new_lock);
}

void layers_finish(void)
{
    wl_list_remove(&new_surface.link);
    wl_list_remove(&new_lock.link);
    free_struts();
}
