/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ipc.h for owbox
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

#ifndef __ob_ipc_h
#define __ob_ipc_h

#include <glib.h>

void ipc_startup(void);
void ipc_shutdown(void);

/*! The socket of the owbox running on $WAYLAND_DISPLAY */
gchar* ipc_socket_path(void);
/*! Send a command to the running owbox and print its reply, returns TRUE if
  it succeeded */
gboolean ipc_send_command(const gchar *command);

#endif
