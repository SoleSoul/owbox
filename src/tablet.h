/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   tablet.h for owbox
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

#ifndef __tablet_h
#define __tablet_h

#include <glib.h>

struct wlr_input_device;

/* Drawing tablets (the tablet-v2 protocol).  A pen acts as the pointer for
   owbox (bindings, frames, menus) and for clients that don't know tablets;
   clients that do get its pressure, tilt and buttons.  Pads (the tablet's
   buttons, rings and strips) go to the focused window. */

void tablet_init(void);
void tablet_finish(void);

/*! A tablet or a tablet pad was plugged in */
void tablet_new(struct wlr_input_device *device);
void tablet_pad_new(struct wlr_input_device *device);

/*! owbox takes the pointer (a grab, the lock screen): the tools leave the
  clients' surfaces */
void tablet_tools_leave(void);

#endif
