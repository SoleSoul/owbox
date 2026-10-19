/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xwayland.h for owbox
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

/* X11 applications through Xwayland, managed as clients like Openbox
   manages X windows */

#ifndef __ob_xwayland_h
#define __ob_xwayland_h

#include <glib.h>

struct wl_client;
struct wl_global;

void xwayland_init(void);
void xwayland_start(void);
void xwayland_finish(void);

/*! Give X windows the cursor theme's default cursor */
void xwayland_update_cursor(void);
/*! Set _NET_WORKAREA from the screen's areas */
void xwayland_update_workareas(void);
/*! Can the client see the global, Xwayland has a global of its own */
gboolean xwayland_global_allowed(const struct wl_client *client,
                                 const struct wl_global *global);

#endif
