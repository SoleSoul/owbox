/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   window.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2003-2007   Dana Jansens
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

#include "window.h"
#include "menuframe.h"
#include "config.h"
#include "client.h"
#include "frame.h"
#include "openbox.h"
#include "server.h"
#include "layers.h"
#include "debug.h"
#include "fwin.h"

#include <wlr/types/wlr_scene.h>

void window_startup(gboolean reconfig)
{
}

void window_shutdown(gboolean reconfig)
{
}

void ob_node_attach(ObNode *node, struct wlr_scene_node *scene_node)
{
    scene_node->data = node;
}

ObNode* ob_node_find(struct wlr_scene_node *scene_node)
{
    while (scene_node) {
        if (scene_node->data)
            return scene_node->data;
        scene_node = scene_node->parent ? &scene_node->parent->node : NULL;
    }
    return NULL;
}

ObNode* window_at(double lx, double ly, struct wlr_surface **surface,
                  double *sx, double *sy)
{
    struct wlr_scene_node *node;
    double nx, ny;

    if (surface) *surface = NULL;

    node = wlr_scene_node_at(&server.scene->tree.node, lx, ly, &nx, &ny);
    if (!node) return NULL;

    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buf);
        if (ss && surface) {
            *surface = ss->surface;
            if (sx) *sx = nx;
            if (sy) *sy = ny;
        }
    }
    return ob_node_find(node);
}

typedef struct {
    struct wlr_surface *surface;
    struct wlr_scene_buffer *found;
} FindBuffer;

static void find_buffer(struct wlr_scene_buffer *buffer, int sx, int sy,
                        void *data)
{
    FindBuffer *f = data;
    struct wlr_scene_surface *ss;

    if (f->found) return;
    ss = wlr_scene_surface_try_from_buffer(buffer);
    if (ss && ss->surface == f->surface)
        f->found = buffer;
}

struct wlr_scene_buffer* window_scene_buffer_for_surface(
    struct wlr_surface *surface)
{
    FindBuffer f = { surface, NULL };
    if (!surface) return NULL;
    wlr_scene_node_for_each_buffer(&server.scene->tree.node, find_buffer, &f);
    return f.found;
}

struct wlr_scene_tree* window_top(ObWindow *self)
{
    switch (self->type) {
    case OB_WINDOW_CLASS_MENUFRAME:
        return WINDOW_AS_MENUFRAME(self)->window->tree;
    case OB_WINDOW_CLASS_CLIENT:
        return WINDOW_AS_CLIENT(self)->frame->window->tree;
    case OB_WINDOW_CLASS_INTERNAL:
    case OB_WINDOW_CLASS_PROMPT:
        return WINDOW_AS_INTERNAL(self)->window->tree;
    case OB_WINDOW_CLASS_LAYER:
        return ((ObLayerSurface*)self)->tree;
    case OB_WINDOW_CLASS_DOCK:
    case OB_WINDOW_CLASS_UNMANAGED:
        break;
    }
    g_assert_not_reached();
    return NULL;
}

ObStackingLayer window_layer(ObWindow *self)
{
    switch (self->type) {
    case OB_WINDOW_CLASS_DOCK:
        return config_dock_layer;
    case OB_WINDOW_CLASS_CLIENT:
        return ((ObClient*)self)->layer;
    case OB_WINDOW_CLASS_MENUFRAME:
    case OB_WINDOW_CLASS_INTERNAL:
        return OB_STACKING_LAYER_INTERNAL;
    case OB_WINDOW_CLASS_PROMPT:
    case OB_WINDOW_CLASS_LAYER:
    case OB_WINDOW_CLASS_UNMANAGED:
        /* not stacked by Openbox's stacking code */
        break;
    }
    g_assert_not_reached();
    return OB_STACKING_LAYER_INVALID;
}

void window_unmanage_all(void)
{
    client_unmanage_all();
}
