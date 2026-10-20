/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   share_menu.h for owbox
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

#ifndef ob__share_menu_h
#define ob__share_menu_h

#include <glib.h>

/*! Called with "Monitor: <name>" or "Window: <identifier>", or NULL when
  nothing was chosen */
typedef void (*ObShareMenuDone)(const gchar *choice, gpointer data);

void share_menu_startup(gboolean reconfig);
void share_menu_shutdown(gboolean reconfig);

/*! Show the menu of screens and/or windows to share */
void share_menu_choose(gboolean monitors, gboolean windows,
                       ObShareMenuDone func, gpointer data);

#endif
