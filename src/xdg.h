/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xdg.h for owbox
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

#ifndef __ob_xdg_h
#define __ob_xdg_h

#include "client.h"

extern const ObClientImpl xdg_client_impl;

struct wlr_xdg_popup;
struct wlr_scene_tree;

void xdg_init(void);
/*! Show a popup in parent_tree, and keep it on its output */
void xdg_popup_add(struct wlr_xdg_popup *popup,
                   struct wlr_scene_tree *parent_tree);
void xdg_finish(void);

#endif
