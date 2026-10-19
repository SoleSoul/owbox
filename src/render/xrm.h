/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xrm.h for owbox
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

#ifndef __render_xrm_h
#define __render_xrm_h

#include <glib.h>

/*! A minimal replacement for Xlib's resource manager (XrmDatabase), enough
  to read Openbox themerc files with the same matching and precedence rules
  as XrmGetResource. */
typedef struct _RrXrmDb RrXrmDb;

RrXrmDb* RrXrmLoadFile(const gchar *path);
RrXrmDb* RrXrmLoadString(const gchar *data);
void     RrXrmFree(RrXrmDb *db);

/*! Look up a fully qualified resource name and class (e.g.
  "window.active.title.bg" and "Window.Active.Title.Bg").  The returned value
  is owned by the database but may be modified in place (as Openbox does
  with g_strstrip). */
gchar*   RrXrmGetResource(RrXrmDb *db, const gchar *name, const gchar *klass);

#endif
