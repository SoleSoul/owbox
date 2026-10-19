/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   output.h for owbox
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

/* Outputs are what Openbox calls monitors (Xinerama screens).  The enabled
   outputs, ordered left to right then top to bottom, are the monitors that
   screen.c works with. */

#ifndef __ob_output_h
#define __ob_output_h

#include "geom.h"

#include <glib.h>
#include <wayland-server-core.h>

struct wlr_output;
struct wlr_scene_output;

typedef struct _ObOutput ObOutput;

struct _ObOutput {
    struct wl_list link; /* ObServer::outputs */
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    /*! The output's area in the layout, in logical (scaled) pixels */
    Rect area;
    /*! Is the output part of the layout (enabled) */
    gboolean in_layout;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

void output_init(void);
void output_finish(void);

/*! The number of monitors */
guint output_count(void);
/*! The n'th monitor */
ObOutput* output_nth(guint n);
/*! The output at a point in the layout, or NULL */
ObOutput* output_at(gint x, gint y);
/*! The output for a wlr_output */
ObOutput* output_from_wlr(struct wlr_output *wlr_output);
/*! The largest scale of all the outputs, what internal textures are
  rendered at */
gfloat output_max_scale(void);

/*! Apply the output settings from the configuration file */
void output_reconfigure(void);

#endif
