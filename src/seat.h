/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   seat.h for owbox
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

/* The input devices, the cursor, and keyboard and pointer focus.  Input is
   turned into ObEvents for event.c (Openbox's event handling), which says
   if the event is used by owbox or goes on to the clients. */

#ifndef __ob_seat_h
#define __ob_seat_h

#include "misc.h"

#include <glib.h>

struct _ObClient;
struct wlr_surface;
struct wlr_input_device;

void seat_init(void);
void seat_finish(void);

/*! Apply the keyboard, pointer and cursor settings from the config */
void seat_reconfigure(void);

/*! Give keyboard focus to a client, NULL to focus nothing.  Returns FALSE if
  that isn't possible right now (a layer surface or the lock screen holds the
  keyboard) */
gboolean seat_focus_client(struct _ObClient *client);
/*! Give keyboard focus to a surface that isn't a client (layer shell,
  unmanaged Xwayland), NULL to give it back to the focused client */
void seat_focus_surface(struct wlr_surface *surface);
/*! The surface with keyboard focus */
struct wlr_surface* seat_focused_surface(void);

/*! Called by grab.c */
void seat_keyboard_grabbed(gboolean grabbed);
void seat_pointer_grabbed(gboolean grabbed);

/*! The pointer's position in layout coordinates */
void seat_pointer_pos(gint *x, gint *y);
/*! Move the pointer */
void seat_pointer_warp(gint x, gint y);
/*! Is a button held down, and which (X numbering) */
gboolean seat_pointer_button_held(void);
guint seat_pointer_held_button(void);
/*! Send the button press that mouse.c is holding back to the client, this is
  the ReplayPointer of X */
void seat_pointer_replay_press(void);

/*! Find what is under the pointer again, for after windows moved or
  appeared, and set the cursor */
void seat_rebase_pointer(void);
/*! Set the cursor for what is under the pointer or the grab */
void seat_update_cursor(void);
/*! Show a cursor from the cursor theme (used for internal windows) */
void seat_set_cursor(ObCursor cursor);

void seat_output_layout_changed(void);

/*! A client was unmanaged or a surface went away, make sure nothing refers
  to it */
void seat_surface_gone(struct wlr_surface *surface);
void seat_client_gone(struct _ObClient *client);

/*! Set the session lock state */
void seat_set_locked(gboolean locked);
gboolean seat_is_locked(void);

/*! Inhibit idleness (idle-inhibit protocol) */
void seat_update_idle_inhibit(void);

#endif
