/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   grab.h for owbox (ported from the Openbox window manager)
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

/* Openbox grabs the X keyboard and pointer for menus, interactive actions
   and moving/resizing.  The compositor sees all input anyways, so a grab
   just means that input goes to owbox instead of the clients. */

#ifndef __grab_h
#define __grab_h

#include "misc.h"

#include <glib.h>

void grab_startup(gboolean reconfig);
void grab_shutdown(gboolean reconfig);

gboolean grab_keyboard_full(gboolean grab);
/*! @param confine If true the pointer is confined to the screen */
gboolean grab_pointer_full(gboolean grab, gboolean owner_events,
                           gboolean confine, ObCursor cur);

#define grab_keyboard() grab_keyboard_full(TRUE)
#define ungrab_keyboard() grab_keyboard_full(FALSE)
#define grab_pointer(o,c,u) grab_pointer_full(TRUE, (o), (c), (u))
#define ungrab_pointer() grab_pointer_full(FALSE, FALSE, FALSE, OB_CURSOR_NONE)

gboolean grab_on_keyboard(void);
gboolean grab_on_pointer(void);
/*! The cursor to show during the pointer grab */
ObCursor grab_pointer_cursor(void);
/*! Change the cursor during a pointer grab */
void grab_set_pointer_cursor(ObCursor cur);

/*! Ends a passive keyboard grab (a key binding being held), there are no
  passive grabs in owbox */
void ungrab_passive_key(void);

#endif
