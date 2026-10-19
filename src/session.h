/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   session.h for owbox
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

/* What openbox-session and openbox-autostart do for Openbox: the session's
   environment, the autostart script and the XDG autostart applications.
   There is no X session management (XSMP) in Wayland. */

#ifndef __ob_session_h
#define __ob_session_h

#include <glib.h>

/*! Set up the environment for the programs started in the session */
void session_environment(void);
/*! Run ~/.config/owbox/autostart and the XDG autostart programs */
void session_autostart(void);
void session_shutdown(void);

/*! There is no session manager to talk to */
static inline gboolean session_connected(void) { return FALSE; }
static inline void session_request_logout(gboolean silent) {}

#endif
