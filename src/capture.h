/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   capture.h for owbox
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

#ifndef __ob_capture_h
#define __ob_capture_h

struct wlr_ext_image_capture_source_v1;
struct wlr_scene;

void capture_init(void);
void capture_finish(void);

/*! The source captures a window's own scene (made for it by its backend) */
void capture_window_scene(struct wlr_ext_image_capture_source_v1 *source,
                          struct wlr_scene *scene);

#endif
