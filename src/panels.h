/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   panels.h for owbox
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

#ifndef __panels_h
#define __panels_h

#include <glib.h>

/* Showing the panels (the TogglePanels action): the bars along the screen's
   edges come up above the windows, which are dimmed, with a launcher if
   one is given, until Escape, a click elsewhere, another window taking the
   focus, or the launcher closing.  With <owbox><panels><autoHide> the
   panels are hidden the rest of the time. */

struct _ObLayerSurface;
struct _ObClient;
struct _ObEvent;
struct wlr_surface;

void panels_startup(gboolean reconfig);
void panels_shutdown(gboolean reconfig);

/*! Show the panels, and run the launcher command (if not NULL) */
void panels_show(const gchar *launcher);
void panels_hide(void);
gboolean panels_shown(void);

/* what ends it */
void panels_layer_mapped(struct _ObLayerSurface *s);
void panels_layer_gone(struct _ObLayerSurface *s);
void panels_focus_changed(struct _ObClient *client);
/*! A window was brought up (the taskbar, an application starting) */
void panels_window_activated(void);
/*! A button was pressed on surface (NULL for owbox's own, or nothing) */
void panels_pointer_pressed(struct wlr_surface *surface);
/*! A key was pressed, returns TRUE if it was used (Escape) */
gboolean panels_key(const struct _ObEvent *e);

#endif
