/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   snap.c for owbox
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

#include "snap.h"
#include "client.h"
#include "frame.h"
#include "screen.h"
#include "config.h"
#include "openbox.h"
#include "server.h"
#include "debug.h"
#include "render/theme.h"

#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>

/*! How far the pointer must move before a snapped window lets go */
#define UNSNAP_THRESHOLD 8

static struct wlr_scene_tree *preview = NULL;
static struct wlr_scene_rect *preview_fill = NULL;
static struct wlr_scene_rect *preview_border[4];

/*! The edges the dragged window would snap to, and on which monitor */
static guint drag_edges = 0;
static guint drag_monitor = 0;
/*! Did the drag let go of a snapped or maximized window already */
static gboolean drag_unsnapped = FALSE;
static gint drag_start_px, drag_start_py;

void snap_startup(gboolean reconfig)
{
    const RrColor *fill, *border;
    float c[4];
    gint i;

    preview = wlr_scene_tree_create(server.stack_tree[OB_STACKING_LAYER_FULLSCREEN]);
    wlr_scene_node_set_enabled(&preview->node, FALSE);

    /* look like the theme, a see through window title with a border */
    fill = ob_rr_theme->a_focused_title->surface.primary;
    border = ob_rr_theme->frame_focused_border_color;
    c[0] = fill->r / 255.0f * 0.35f;
    c[1] = fill->g / 255.0f * 0.35f;
    c[2] = fill->b / 255.0f * 0.35f;
    c[3] = 0.35f;
    preview_fill = wlr_scene_rect_create(preview, 1, 1, c);
    c[0] = border->r / 255.0f;
    c[1] = border->g / 255.0f;
    c[2] = border->b / 255.0f;
    c[3] = 1.0f;
    for (i = 0; i < 4; ++i)
        preview_border[i] = wlr_scene_rect_create(preview, 1, 1, c);
}

void snap_shutdown(gboolean reconfig)
{
    wlr_scene_node_destroy(&preview->node);
    preview = NULL;
}

/*! The frame area for snapping to the edges of a monitor */
static gboolean snap_area(guint edges, guint monitor, guint desktop,
                          Rect *out)
{
    Rect *a = screen_area(desktop, monitor, NULL);
    gint hw = a->width / 2, hh = a->height / 2;

    *out = *a;
    if ((edges & WLR_EDGE_LEFT) && !(edges & WLR_EDGE_RIGHT))
        out->width = hw;
    if ((edges & WLR_EDGE_RIGHT) && !(edges & WLR_EDGE_LEFT)) {
        out->x += hw;
        out->width = a->width - hw;
    }
    if ((edges & WLR_EDGE_TOP) && !(edges & WLR_EDGE_BOTTOM))
        out->height = hh;
    if ((edges & WLR_EDGE_BOTTOM) && !(edges & WLR_EDGE_TOP)) {
        out->y += hh;
        out->height = a->height - hh;
    }
    g_slice_free(Rect, a);
    return edges != 0;
}

void snap_client(ObClient *c, guint edges, guint monitor)
{
    Rect r;

    if (!c->frame || !(c->functions & OB_CLIENT_FUNC_MOVE) ||
        !(c->functions & OB_CLIENT_FUNC_RESIZE))
        return;

    if (!edges) {
        snap_restore(c);
        return;
    }

    /* the top edge is maximize */
    if (edges == WLR_EDGE_TOP && config_snap_top_maximize) {
        if (c->snap_edges)
            snap_restore(c);
        client_maximize(c, TRUE, 0);
        return;
    }

    if (c->max_horz || c->max_vert)
        client_maximize(c, FALSE, 0);
    if (c->fullscreen)
        client_fullscreen(c, FALSE);

    if (!c->snap_edges)
        c->pre_snap_area = c->area;
    c->snap_edges = edges;

    snap_area(edges, monitor, c->desktop, &r);
    frame_rect_to_client(c->frame, &r);
    client_configure(c, r.x, r.y, r.width, r.height, TRUE, TRUE, FALSE);
    if (c->impl->change_state)
        c->impl->change_state(c);
}

void snap_restore(ObClient *c)
{
    if (!c->snap_edges) return;
    c->snap_edges = 0;
    client_configure(c, c->pre_snap_area.x, c->pre_snap_area.y,
                     c->pre_snap_area.width, c->pre_snap_area.height,
                     TRUE, TRUE, FALSE);
    if (c->impl->change_state)
        c->impl->change_state(c);
}

static void show_preview(const Rect *r)
{
    gint bw = MAX(ob_rr_theme->fbwidth, 2);

    wlr_scene_node_set_position(&preview->node, r->x, r->y);
    wlr_scene_rect_set_size(preview_fill, r->width, r->height);
    wlr_scene_rect_set_size(preview_border[0], r->width, bw);
    wlr_scene_rect_set_size(preview_border[1], r->width, bw);
    wlr_scene_node_set_position(&preview_border[1]->node, 0,
                                r->height - bw);
    wlr_scene_rect_set_size(preview_border[2], bw, r->height);
    wlr_scene_rect_set_size(preview_border[3], bw, r->height);
    wlr_scene_node_set_position(&preview_border[3]->node, r->width - bw, 0);
    wlr_scene_node_raise_to_top(&preview->node);
    wlr_scene_node_set_enabled(&preview->node, TRUE);
}

static void hide_preview(void)
{
    if (preview)
        wlr_scene_node_set_enabled(&preview->node, FALSE);
}

void snap_drag_begin(ObClient *c)
{
    drag_edges = 0;
    drag_unsnapped = FALSE;
    screen_pointer_pos(&drag_start_px, &drag_start_py);
}

void snap_drag_update(ObClient *c, gint px, gint py)
{
    guint m;
    const Rect *a;
    guint edges = 0;
    gint range = config_snap_range, corner = config_snap_corner_range;

    if (!config_snap_enabled || !(c->functions & OB_CLIENT_FUNC_RESIZE)) {
        drag_edges = 0;
        hide_preview();
        return;
    }

    m = screen_find_monitor_point(px, py);
    a = screen_physical_area_monitor(m);

    if (px < a->x + range) edges |= WLR_EDGE_LEFT;
    else if (px >= a->x + a->width - range) edges |= WLR_EDGE_RIGHT;
    if (py < a->y + range) edges |= WLR_EDGE_TOP;
    else if (py >= a->y + a->height - range) edges |= WLR_EDGE_BOTTOM;

    /* near a corner along an edge counts as the corner */
    if (edges == WLR_EDGE_LEFT || edges == WLR_EDGE_RIGHT) {
        if (py < a->y + corner) edges |= WLR_EDGE_TOP;
        else if (py >= a->y + a->height - corner) edges |= WLR_EDGE_BOTTOM;
    } else if (edges == WLR_EDGE_TOP || edges == WLR_EDGE_BOTTOM) {
        if (px < a->x + corner) edges |= WLR_EDGE_LEFT;
        else if (px >= a->x + a->width - corner) edges |= WLR_EDGE_RIGHT;
    }
    /* the bottom edge alone does nothing */
    if (edges == WLR_EDGE_BOTTOM)
        edges = 0;

    /* don't snap at the edge between two monitors */
    if (edges) {
        guint i;
        for (i = 0; i < screen_num_monitors; ++i) {
            const Rect *o = screen_physical_area_monitor(i);
            if (i == m) continue;
            if ((edges & WLR_EDGE_LEFT) && RECT_CONTAINS(*o, a->x - 1, py))
                edges &= ~WLR_EDGE_LEFT;
            if ((edges & WLR_EDGE_RIGHT) &&
                RECT_CONTAINS(*o, a->x + a->width, py))
                edges &= ~WLR_EDGE_RIGHT;
            if ((edges & WLR_EDGE_TOP) && RECT_CONTAINS(*o, px, a->y - 1))
                edges &= ~WLR_EDGE_TOP;
            if ((edges & WLR_EDGE_BOTTOM) &&
                RECT_CONTAINS(*o, px, a->y + a->height))
                edges &= ~WLR_EDGE_BOTTOM;
        }
        if (edges == WLR_EDGE_BOTTOM)
            edges = 0;
    }

    if (edges == drag_edges && m == drag_monitor)
        return;
    drag_edges = edges;
    drag_monitor = m;

    if (edges && config_snap_preview) {
        Rect r;
        snap_area(edges, m, c->desktop, &r);
        if (edges == WLR_EDGE_TOP && config_snap_top_maximize) {
            Rect *full = screen_area(c->desktop, m, NULL);
            r = *full;
            g_slice_free(Rect, full);
        }
        show_preview(&r);
    } else
        hide_preview();
}

void snap_drag_end(ObClient *c, gboolean cancel)
{
    guint edges = drag_edges;

    hide_preview();
    drag_edges = 0;
    if (!cancel && edges && c && c->managed)
        snap_client(c, edges, drag_monitor);
}

void snap_drag_unsnap(ObClient *c, gint px, gint py,
                      gint *start_x, gint *start_y,
                      gint *start_cx, gint *start_cy,
                      gint *cur_x, gint *cur_y, gint *cur_w, gint *cur_h)
{
    gboolean maxed = c->max_horz && c->max_vert;
    Rect old, want;
    gint fw, nfw;

    if (drag_unsnapped || !config_snap_enabled) return;
    if (!c->snap_edges && !(maxed && config_snap_drag_unmaximize))
        return;
    if (ABS(px - drag_start_px) < UNSNAP_THRESHOLD &&
        ABS(py - drag_start_py) < UNSNAP_THRESHOLD)
        return;

    drag_unsnapped = TRUE;
    old = c->frame->area;
    fw = old.width;

    if (c->snap_edges) {
        want = c->pre_snap_area;
        c->snap_edges = 0;
    } else {
        want = c->pre_max_area;
        if (want.width <= 0 || want.height <= 0) return;
        /* unmaximize without moving, the drag decides where it goes */
        c->max_horz = c->max_vert = FALSE;
        RECT_SET(c->pre_max_area, 0, 0, 0, 0);
        client_setup_decor_and_functions(c, FALSE);
    }

    /* keep the pointer at the same place across the titlebar, and the same
       distance from the top */
    nfw = want.width + c->frame->size.left + c->frame->size.right;
    *cur_w = want.width;
    *cur_h = want.height;
    *cur_x = px - (gint)((gint64)(px - old.x) * nfw / MAX(fw, 1));
    *cur_y = py - (*start_y - old.y);
    *start_cx = *cur_x;
    *start_cy = *cur_y;
    *start_x = px;
    *start_y = py;

    client_configure(c, *cur_x, *cur_y, *cur_w, *cur_h, TRUE, TRUE, FALSE);
    if (c->impl->change_state)
        c->impl->change_state(c);
}
