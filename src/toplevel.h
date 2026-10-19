/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   toplevel.h for owbox
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

/* The protocols other clients use to see and control the windows and
   desktops: taskbars (foreign toplevel), pagers (ext-workspace), launchers
   (xdg-activation).  This is the Wayland side of the EWMH root window
   properties and messages Openbox handles (_NET_CLIENT_LIST,
   _NET_ACTIVE_WINDOW, _NET_CURRENT_DESKTOP, _NET_DESKTOP_NAMES, ...). */

#ifndef __ob_toplevel_h
#define __ob_toplevel_h

#include "render/render.h"

#include <glib.h>

struct _ObClient;

void toplevel_init(void);
void toplevel_finish(void);

void toplevel_new(struct _ObClient *c);
void toplevel_destroy(struct _ObClient *c);
void toplevel_update_title(struct _ObClient *c);
void toplevel_update_app_id(struct _ObClient *c);
void toplevel_update_state(struct _ObClient *c);
void toplevel_update_geometry(struct _ObClient *c);
void toplevel_update_desktop(struct _ObClient *c);
void toplevel_update_focus(struct _ObClient *old, struct _ObClient *new_);
void toplevel_update_stacking(void);
/*! The number, names or current desktop changed */
void toplevel_update_desktops(void);

/*! The time the client's launch was requested, from its xdg-activation
  token, 0 if unknown */
guint32 toplevel_activation_time(struct _ObClient *c);
/*! The desktop the client was launched on, from its activation token */
gboolean toplevel_activation_desktop(struct _ObClient *c, guint *desktop);
/*! The client's icon (from the xdg-toplevel-icon protocol or its desktop
  file), or NULL */
RrImage* toplevel_find_icon(struct _ObClient *c);
/*! Make an xdg-activation token for a program owbox launches, which lets
  its window take the focus.  Returns NULL if that isn't possible. */
const gchar* toplevel_launch_token(const gchar *app_id);
/*! Is the client a modal dialog (xdg-dialog protocol) */
gboolean toplevel_is_modal(struct _ObClient *c);

/*! The window's ext-foreign-toplevel identifier, or NULL */
const gchar* toplevel_identifier(struct _ObClient *c);

#endif
