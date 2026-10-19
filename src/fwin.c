/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   fwin.c for owbox
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

#include "fwin.h"
#include "buffer.h"
#include "render/color.h"

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

ObFWin* fwin_new(ObFWin *parent, struct wlr_scene_tree *parent_tree,
                 ObNodeType type, ObWindow *owner)
{
    ObFWin *w = g_slice_new0(ObFWin);
    struct wlr_scene_tree *ptree = parent ? parent->tree : parent_tree;

    w->node.type = type;
    w->node.owner = owner;
    w->parent = parent;
    if (parent)
        parent->children = g_list_append(parent->children, w);
    w->tree = wlr_scene_tree_create(ptree);
    ob_node_attach(&w->node, &w->tree->node);
    wlr_scene_node_set_enabled(&w->tree->node, FALSE);
    w->width = w->height = 1;
    return w;
}

static void forget_children(ObFWin *w)
{
    GList *it;
    for (it = w->children; it; it = g_list_next(it))
        ((ObFWin*)it->data)->parent = NULL;
    g_list_free(w->children);
    w->children = NULL;
}

void fwin_destroy(ObFWin *w)
{
    if (!w) return;
    if (w->parent)
        w->parent->children = g_list_remove(w->parent->children, w);
    forget_children(w);
    wlr_scene_node_destroy(&w->tree->node);
    g_slice_free(ObFWin, w);
}

void fwin_free(ObFWin *w)
{
    if (!w) return;
    if (w->parent)
        w->parent->children = g_list_remove(w->parent->children, w);
    forget_children(w);
    g_slice_free(ObFWin, w);
}

/* The part of the window that can be seen, in its own coordinates, after
   clipping it to itself and all its parents like X does */
static gboolean visible_box(ObFWin *w, gint *x0, gint *y0, gint *x1, gint *y1)
{
    ObFWin *a;
    gint ox = 0, oy = 0; /* the ancestor's position in w's coordinates */
    ObFWin *c = w;

    *x0 = 0;
    *y0 = 0;
    *x1 = w->width;
    *y1 = w->height;
    for (a = w->parent; a; c = a, a = a->parent) {
        /* move to a's coordinates: c's content is at c->x + border */
        ox -= c->x + c->border_width;
        oy -= c->y + c->border_width;
        *x0 = MAX(*x0, ox);
        *y0 = MAX(*y0, oy);
        *x1 = MIN(*x1, ox + a->width);
        *y1 = MIN(*y1, oy + a->height);
    }
    return *x1 > *x0 && *y1 > *y0;
}

static void apply_clip(ObFWin *w)
{
    gint x0, y0, x1, y1;
    gboolean vis = visible_box(w, &x0, &y0, &x1, &y1);

    if (w->buffer) {
        gint bx1 = MIN(x1, w->buf_width), by1 = MIN(y1, w->buf_height);
        if (vis && bx1 > x0 && by1 > y0) {
            struct wlr_fbox src = { x0, y0, bx1 - x0, by1 - y0 };
            wlr_scene_buffer_set_source_box(w->buffer, &src);
            wlr_scene_buffer_set_dest_size(w->buffer, bx1 - x0, by1 - y0);
            wlr_scene_node_set_position(&w->buffer->node, x0, y0);
            wlr_scene_node_set_enabled(&w->buffer->node, TRUE);
        } else
            wlr_scene_node_set_enabled(&w->buffer->node, FALSE);
    }
    if (w->rect) {
        if (vis) {
            wlr_scene_rect_set_size(w->rect, x1 - x0, y1 - y0);
            wlr_scene_node_set_position(&w->rect->node, x0, y0);
            wlr_scene_node_set_enabled(&w->rect->node, TRUE);
        } else
            wlr_scene_node_set_enabled(&w->rect->node, FALSE);
    }
}

/* the window and its children's visible parts changed */
static void reclip(ObFWin *w)
{
    GList *it;
    apply_clip(w);
    for (it = w->children; it; it = g_list_next(it))
        reclip(it->data);
}

void fwin_input_only(ObFWin *w)
{
    static const float clear[4] = { 0, 0, 0, 0 };
    w->rect = wlr_scene_rect_create(w->tree, w->width, w->height, clear);
    wlr_scene_node_lower_to_bottom(&w->rect->node);
    apply_clip(w);
}

static void place_tree(ObFWin *w)
{
    /* the position of an X window is the outside of its border */
    wlr_scene_node_set_position(&w->tree->node, w->x + w->border_width,
                                w->y + w->border_width);
}

void fwin_move(ObFWin *w, gint x, gint y)
{
    /* like X ignores requests on window None */
    if (!w) return;
    w->x = x;
    w->y = y;
    place_tree(w);
    reclip(w);
}

void fwin_resize(ObFWin *w, gint width, gint height)
{
    if (!w) return;
    w->width = MAX(width, 1);
    w->height = MAX(height, 1);
    if (w->border)
        wlr_scene_rect_set_size(w->border, w->width + 2 * w->border_width,
                                w->height + 2 * w->border_width);
    reclip(w);
}

void fwin_move_resize(ObFWin *w, gint x, gint y, gint width, gint height)
{
    if (!w) return;
    fwin_move(w, x, y);
    fwin_resize(w, width, height);
}

void fwin_map(ObFWin *w)
{
    if (!w) return;
    w->mapped = TRUE;
    wlr_scene_node_set_enabled(&w->tree->node, TRUE);
}

void fwin_unmap(ObFWin *w)
{
    if (!w) return;
    w->mapped = FALSE;
    wlr_scene_node_set_enabled(&w->tree->node, FALSE);
}

void fwin_raise(ObFWin *w)
{
    if (!w) return;
    wlr_scene_node_raise_to_top(&w->tree->node);
}

/* the background goes below the window's children, and above its border */
static void lower_background(ObFWin *w, struct wlr_scene_node *n)
{
    wlr_scene_node_lower_to_bottom(n);
    if (w->border)
        wlr_scene_node_lower_to_bottom(&w->border->node);
}

void fwin_set_color(ObFWin *w, const RrColor *c)
{
    if (!w) return;
    float color[4] = { c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, 1.0f };
    if (w->buffer) {
        wlr_scene_node_destroy(&w->buffer->node);
        w->buffer = NULL;
    }
    if (!w->rect) {
        w->rect = wlr_scene_rect_create(w->tree, w->width, w->height, color);
        lower_background(w, &w->rect->node);
    } else
        wlr_scene_rect_set_color(w->rect, color);
    apply_clip(w);
}

void fwin_set_surface(ObFWin *w, cairo_surface_t *s)
{
    if (!w) {
        if (s) cairo_surface_destroy(s);
        return;
    }
    struct wlr_buffer *buf;

    if (!s) {
        if (w->buffer) {
            wlr_scene_node_destroy(&w->buffer->node);
            w->buffer = NULL;
        }
        return;
    }
    w->buf_width = cairo_image_surface_get_width(s);
    w->buf_height = cairo_image_surface_get_height(s);
    buf = ob_buffer_from_cairo(s);
    if (!w->buffer) {
        w->buffer = wlr_scene_buffer_create(w->tree, buf);
        lower_background(w, &w->buffer->node);
    } else
        wlr_scene_buffer_set_buffer(w->buffer, buf);
    wlr_buffer_drop(buf);
    apply_clip(w);
}

void fwin_paint(ObFWin *w, RrAppearance *a, gint width, gint height)
{
    if (!w) return;
    cairo_surface_t *s = RrPaintSurface(a, width, height);
    if (s)
        fwin_set_surface(w, s);
}

void fwin_set_border(ObFWin *w, gint width, const RrColor *c)
{
    if (!w) return;
    float color[4] = { c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, 1.0f };

    w->border_width = width;
    if (width <= 0) {
        if (w->border) {
            wlr_scene_node_destroy(&w->border->node);
            w->border = NULL;
        }
        place_tree(w);
        return;
    }
    if (!w->border) {
        w->border = wlr_scene_rect_create(w->tree, 1, 1, color);
        wlr_scene_node_lower_to_bottom(&w->border->node);
    } else
        wlr_scene_rect_set_color(w->border, color);
    wlr_scene_node_set_position(&w->border->node, -width, -width);
    wlr_scene_rect_set_size(w->border, w->width + 2 * width,
                            w->height + 2 * width);
    place_tree(w);
    reclip(w);
}
