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
    w->tree = wlr_scene_tree_create(ptree);
    ob_node_attach(&w->node, &w->tree->node);
    wlr_scene_node_set_enabled(&w->tree->node, FALSE);
    w->width = w->height = 1;
    return w;
}

void fwin_destroy(ObFWin *w)
{
    if (!w) return;
    wlr_scene_node_destroy(&w->tree->node);
    g_slice_free(ObFWin, w);
}

void fwin_free(ObFWin *w)
{
    g_slice_free(ObFWin, w);
}

void fwin_input_only(ObFWin *w)
{
    static const float clear[4] = { 0, 0, 0, 0 };
    w->rect = wlr_scene_rect_create(w->tree, w->width, w->height, clear);
    wlr_scene_node_lower_to_bottom(&w->rect->node);
}

static void place_tree(ObFWin *w)
{
    /* the position of an X window is the outside of its border */
    wlr_scene_node_set_position(&w->tree->node, w->x + w->border_width,
                                w->y + w->border_width);
}

void fwin_move(ObFWin *w, gint x, gint y)
{
    w->x = x;
    w->y = y;
    place_tree(w);
}

void fwin_resize(ObFWin *w, gint width, gint height)
{
    w->width = MAX(width, 1);
    w->height = MAX(height, 1);
    if (w->rect)
        wlr_scene_rect_set_size(w->rect, w->width, w->height);
    if (w->border)
        wlr_scene_rect_set_size(w->border, w->width + 2 * w->border_width,
                                w->height + 2 * w->border_width);
}

void fwin_move_resize(ObFWin *w, gint x, gint y, gint width, gint height)
{
    fwin_move(w, x, y);
    fwin_resize(w, width, height);
}

void fwin_map(ObFWin *w)
{
    w->mapped = TRUE;
    wlr_scene_node_set_enabled(&w->tree->node, TRUE);
}

void fwin_unmap(ObFWin *w)
{
    w->mapped = FALSE;
    wlr_scene_node_set_enabled(&w->tree->node, FALSE);
}

void fwin_raise(ObFWin *w)
{
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
}

void fwin_set_surface(ObFWin *w, cairo_surface_t *s)
{
    struct wlr_buffer *buf;

    if (!s) {
        if (w->buffer) {
            wlr_scene_node_destroy(&w->buffer->node);
            w->buffer = NULL;
        }
        return;
    }
    buf = ob_buffer_from_cairo(s);
    if (!w->buffer) {
        w->buffer = wlr_scene_buffer_create(w->tree, buf);
        lower_background(w, &w->buffer->node);
    } else
        wlr_scene_buffer_set_buffer(w->buffer, buf);
    wlr_buffer_drop(buf);
}

void fwin_paint(ObFWin *w, RrAppearance *a, gint width, gint height)
{
    cairo_surface_t *s = RrPaintSurface(a, width, height);
    if (s)
        fwin_set_surface(w, s);
}

void fwin_set_border(ObFWin *w, gint width, const RrColor *c)
{
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
}
