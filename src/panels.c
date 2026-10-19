/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   panels.c for owbox
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

#include "panels.h"
#include "client.h"
#include "config.h"
#include "debug.h"
#include "event.h"
#include "focus.h"
#include "layers.h"
#include "seat.h"
#include "server.h"
#include "buffer.h"

#include <cairo.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

/* how long the panels and the dim take to come and go */
#define FADE_TIME (150 * 1000) /* microseconds */
#define FADE_STEP 8            /* milliseconds */

static gboolean shown = FALSE;
/*! How far shown it is, 0 to 1, while fading */
static gdouble progress = 0;
/*! A black pixel stretched over the monitors, which clicks go through */
static struct wlr_scene_buffer *dim = NULL;
static guint fade_timer = 0;
static gint64 fade_last;
/*! The focused window when they were shown */
static ObClient *focus_at_show = NULL;
/*! The launcher's layer surfaces, the ones that took the keyboard while the
   panels were shown */
static GList *launchers = NULL;
/*! Ignore the launchers closing, owbox closes them */
static gboolean closing = FALSE;

static void set_buffer_opacity(struct wlr_scene_buffer *buffer, gint sx,
                               gint sy, gpointer data)
{
    wlr_scene_buffer_set_opacity(buffer, *(gfloat*)data);
}

static void panel_opacity(ObLayerSurface *s, gpointer data)
{
    wlr_scene_node_for_each_buffer(&s->tree->node, set_buffer_opacity,
                                   data);
}

static void apply_progress(void)
{
    /* ease out, quick at first */
    const gfloat p = 1.0f - (1.0f - progress) * (1.0f - progress);

    wlr_scene_buffer_set_opacity(dim, config_panels_dim * p);
    wlr_scene_node_set_enabled(&dim->node, progress > 0 &&
                               config_panels_dim > 0);
    /* panels that are always there don't fade */
    if (config_panels_autohide)
        layers_for_each_panel(panel_opacity, (gpointer)&p);
}

static void finish_hide(void)
{
    /* fullscreen windows go back above the panels */
    wlr_scene_node_place_above(
        &server.stack_tree[OB_STACKING_LAYER_FULLSCREEN]->node,
        &server.layer_tree[OB_LAYER_SHELL_TOP]->node);
    wlr_scene_node_set_enabled(&dim->node, FALSE);
    layers_place_panels();
    seat_rebase_pointer();
}

static gboolean fade_func(gpointer data)
{
    const gint64 now = g_get_monotonic_time();
    const gdouble d = (gdouble)(now - fade_last) / FADE_TIME;

    fade_last = now;
    progress = CLAMP(progress + (shown ? d : -d), 0.0, 1.0);
    apply_progress();
    if ((shown && progress >= 1.0) || (!shown && progress <= 0.0)) {
        fade_timer = 0;
        if (!shown)
            finish_hide();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void start_fade(void)
{
    fade_last = g_get_monotonic_time();
    if (!fade_timer)
        fade_timer = g_timeout_add(FADE_STEP, fade_func, NULL);
}

gboolean panels_shown(void)
{
    return shown;
}

void panels_show(const gchar *launcher)
{
    struct wlr_box all;

    if (shown || seat_is_locked()) return;
    ob_debug("Showing the panels");
    shown = TRUE;
    focus_at_show = focus_client;

    /* the dim covers every monitor, fullscreen windows included */
    wlr_output_layout_get_box(server.output_layout, NULL, &all);
    wlr_scene_node_set_position(&dim->node, all.x, all.y);
    wlr_scene_buffer_set_dest_size(dim, all.width, all.height);
    wlr_scene_node_lower_to_bottom(&dim->node);
    wlr_scene_node_place_below(
        &server.stack_tree[OB_STACKING_LAYER_FULLSCREEN]->node,
        &server.panels_tree->node);
    layers_place_panels();
    apply_progress();
    start_fade();

    if (launcher) {
        gchar **argv = NULL;
        GError *e = NULL;
        if (!g_shell_parse_argv(launcher, NULL, &argv, &e) ||
            !g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                           NULL, &e))
        {
            g_message("Unable to run the launcher %s: %s", launcher,
                      e->message);
            g_error_free(e);
        }
        g_strfreev(argv);
    }
    seat_rebase_pointer();
}

void panels_hide(void)
{
    GList *it, *close;

    if (!shown) return;
    ob_debug("Hiding the panels");
    shown = FALSE;
    focus_at_show = NULL;

    /* the launcher goes with them */
    close = launchers;
    launchers = NULL;
    closing = TRUE;
    for (it = close; it; it = g_list_next(it))
        layers_close(it->data);
    closing = FALSE;
    g_list_free(close);

    start_fade();
}

/* ---- what ends it ---- */

void panels_layer_mapped(ObLayerSurface *s)
{
    /* a launcher started while they are shown */
    if (shown && !layers_is_panel(s) && layers_surface_wants_keyboard(s) &&
        !g_list_find(launchers, s))
        launchers = g_list_prepend(launchers, s);
}

void panels_layer_gone(ObLayerSurface *s)
{
    GList *it = g_list_find(launchers, s);

    if (!it) return;
    launchers = g_list_delete_link(launchers, it);
    /* closed by itself, Escape in it or something started from it */
    if (!closing && !launchers)
        panels_hide();
}

void panels_focus_changed(ObClient *client)
{
    if (shown && client && client != focus_at_show)
        panels_hide();
}

void panels_window_activated(void)
{
    panels_hide();
}

void panels_pointer_pressed(struct wlr_surface *surface)
{
    ObLayerSurface *s;

    if (!shown) return;
    s = surface ? layers_surface_owner(surface) : NULL;
    if (s && (layers_is_panel(s) || g_list_find(launchers, s)))
        return;
    panels_hide();
}

gboolean panels_key(const ObEvent *e)
{
    ObLayerSurface *s;

    if (!shown || e->type != OB_EVENT_KEY_PRESS ||
        e->sym != XKB_KEY_Escape)
        return FALSE;
    /* the launcher has the keyboard, Escape is for it, and closing it ends
       this */
    s = layers_keyboard_surface();
    if (s && g_list_find(launchers, s))
        return FALSE;
    panels_hide();
    return TRUE;
}

/* ---- startup ---- */

static bool dim_accepts_input(struct wlr_scene_buffer *buffer,
                              double *sx, double *sy)
{
    return FALSE;
}

void panels_startup(gboolean reconfig)
{
    if (!reconfig) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        1, 1);
        struct wlr_buffer *b;
        cairo_t *cr = cairo_create(s);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        /* the buffer owns the surface */
        b = ob_buffer_from_cairo(s);
        dim = wlr_scene_buffer_create(server.panels_tree, b);
        wlr_buffer_drop(b);
        dim->point_accepts_input = dim_accepts_input;
        wlr_scene_node_set_enabled(&dim->node, FALSE);
    }
    /* <autoHide> may have changed */
    if (!shown) {
        const gfloat opaque = 1.0f;
        layers_for_each_panel(panel_opacity, (gpointer)&opaque);
    }
    layers_place_panels();
    layers_arrange_all();
}

void panels_shutdown(gboolean reconfig)
{
    if (reconfig) return;
    if (fade_timer) g_source_remove(fade_timer);
    fade_timer = 0;
    g_list_free(launchers);
    launchers = NULL;
    shown = FALSE;
    dim = NULL; /* destroyed with the scene */
}
