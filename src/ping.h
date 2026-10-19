/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ping.h for the Openbox window manager
   Copyright (c) 2008        Dana Jansens
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

#ifndef __ping_h
#define __ping_h

#include <glib.h>

struct _ObClient;

/*!
  Notifies when the client application isn't responding to pings, or when it
  starts responding again.
  @param dead TRUE if the app isn't responding, FALSE if it starts responding
              again
*/
typedef void (*ObPingEventHandler)(struct _ObClient *c, gboolean dead);

void ping_startup(gboolean reconfigure);
void ping_shutdown(gboolean reconfigure);

/*! Start pinging the client until it answers, if it can be pinged */
void ping_start(struct _ObClient *c, ObPingEventHandler h);
void ping_stop(struct _ObClient *c);

#endif
