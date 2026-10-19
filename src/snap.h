/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   snap.h for owbox
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

/* Window snapping (tiling to halves and quarters of a monitor), the most
   requested feature Openbox never had.  Dropping a window that is being
   dragged at a monitor's edge or corner snaps it there, and dragging it
   away puts it back to its size from before.  The SnapToEdge action does
   the same from the keyboard. */

#ifndef __ob_snap_h
#define __ob_snap_h

#include <glib.h>

struct _ObClient;

void snap_startup(gboolean reconfig);
void snap_shutdown(gboolean reconfig);

/*! Snap a client to the edges (wlr_edges) of the monitor, 0 unsnaps */
void snap_client(struct _ObClient *c, guint edges, guint monitor);
/*! Put a snapped client back to its size from before it was snapped */
void snap_restore(struct _ObClient *c);

/* The moveresize code calls these while a window is dragged */
void snap_drag_begin(struct _ObClient *c);
void snap_drag_update(struct _ObClient *c, gint px, gint py);
void snap_drag_end(struct _ObClient *c, gboolean cancel);
/*! When a snapped window is dragged, give it back its size, adjusting the
  drag's starting point to keep the window under the pointer */
void snap_drag_unsnap(struct _ObClient *c, gint px, gint py,
                      gint *start_x, gint *start_y,
                      gint *start_cx, gint *start_cy,
                      gint *cur_x, gint *cur_y, gint *cur_w, gint *cur_h);

#endif
