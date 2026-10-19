/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   layers.h for owbox
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

/* Layer shell surfaces (panels, docks, wallpapers, launchers, notifications)
   and the session lock.  Panels reserve space with an exclusive zone, which
   becomes a strut, just like an X panel's _NET_WM_STRUT_PARTIAL. */

#ifndef __ob_layers_h
#define __ob_layers_h

#include "window.h"
#include "geom.h"

#include <glib.h>
#include <wayland-server-core.h>

struct wlr_surface;
struct wlr_xdg_popup;
struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;
struct _ObOutput;

typedef struct _ObLayerSurface ObLayerSurface;

struct _ObLayerSurface {
    ObWindow obwin;
    ObNode node;

    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wlr_scene_tree *tree;
    /*! Where its popups go, above the windows */
    struct wlr_scene_tree *popup_tree;
    struct _ObOutput *output;
    gboolean mapped;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_popup;
};

void layers_init(void);
void layers_finish(void);

/*! Position the layer surfaces on all outputs and find the space they
  reserve */
void layers_arrange_all(void);
void layers_output_destroyed(struct _ObOutput *output);

/*! The struts (StrutPartial*) reserved by the layer surfaces, one per
  output */
GList* layers_struts(void);

/*! Should clicks on this layer surface be handled like clicks on the root
  window (wallpapers in the background layer) */
gboolean layers_surface_is_root(ObWindow *win);
/*! A layer surface was clicked, it may want the keyboard */
void layers_surface_clicked(struct wlr_surface *surface);

struct wlr_scene_tree* layers_popup_parent_tree(struct wlr_surface *parent);
void layers_popup_unconstrain(struct wlr_xdg_popup *popup);

/* owbox: panels, bars along an edge of the screen (layers_is_panel), which
   can hide until panels.c shows them */

gboolean layers_is_panel(ObLayerSurface *s);
/*! Put the panels where they go for panels_shown() */
void layers_place_panels(void);
void layers_for_each_panel(void (*func)(ObLayerSurface *s, gpointer data),
                           gpointer data);
/*! The layer surface a surface (or a popup of it) belongs to, or NULL */
ObLayerSurface* layers_surface_owner(struct wlr_surface *surface);
/*! The layer surface with the keyboard focus, or NULL */
ObLayerSurface* layers_keyboard_surface(void);
gboolean layers_surface_wants_keyboard(ObLayerSurface *s);
/*! Ask a layer surface's client to close it */
void layers_close(ObLayerSurface *s);

/*! Is the surface a lock screen surface */
gboolean layers_surface_is_lock(struct wlr_surface *surface);

#endif
