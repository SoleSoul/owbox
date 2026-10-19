/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ping.h for owbox
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

/* Openbox pings clients with _NET_WM_PING.  For Wayland clients wlroots
   does the pinging (xdg_wm_base.ping), see xdg.c, which reports to
   client_ping_event(). */

#ifndef __ping_h
#define __ping_h

#include <glib.h>

static inline void ping_startup(gboolean reconfigure) {}
static inline void ping_shutdown(gboolean reconfigure) {}

#endif
