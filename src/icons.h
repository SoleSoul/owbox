/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   icons.h for owbox
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

/* X windows carry their icons in _NET_WM_ICON.  Wayland windows have an
   app id, whose desktop file names an icon in the icon theme. */

#ifndef __ob_icons_h
#define __ob_icons_h

#include "render/render.h"

struct _ObClient;

void icons_startup(gboolean reconfig);
void icons_shutdown(gboolean reconfig);

/*! The icon for a client, a new reference, or NULL */
RrImage* icons_for_client(struct _ObClient *c);
/*! Load an icon by a file path or an icon theme name, a new reference, or
  NULL */
RrImage* icons_load(const gchar *name_or_path, gint size);

struct sfdo_desktop_db;

/*! The installed applications' desktop entries, reloaded when they change */
struct sfdo_desktop_db* icons_desktop_db(void);

#endif
