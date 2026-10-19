/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   buffer.h for owbox
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

#ifndef __ob_buffer_h
#define __ob_buffer_h

#include <cairo.h>

struct wlr_buffer;

/*! Wrap an ARGB32 cairo image surface in a wlr_buffer.  The buffer takes
  over the caller's reference to the surface.  Drop the buffer with
  wlr_buffer_drop() once it has been handed to the scene. */
struct wlr_buffer* ob_buffer_from_cairo(cairo_surface_t *surface);

#endif
