/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   idle.h for owbox
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

#ifndef __idle_h
#define __idle_h

#include <glib.h>

/* Turning the monitors off when idle, and locking the screen then, before
   suspending and when logind asks (<owbox><idle>).  What swayidle does,
   without having to set it up. */

void idle_startup(void);
void idle_shutdown(void);
/*! The settings changed */
void idle_reconfigure(void);

/*! There was input */
void idle_activity(void);
/*! A visible window keeps the screen on (a video) or stopped doing so */
void idle_set_inhibited(gboolean inhibited);
/*! The session became locked */
void idle_locked(void);

/*! Run the screen locker, unless the screen is locked */
void idle_lock(void);

#endif
