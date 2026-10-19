/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   fwin.h for owbox
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

/* Openbox draws its decorations, menus and popups with trees of plain X
   windows.  An ObFWin is a scene graph stand-in for one, with the same
   semantics: positioned relative to its parent, unmapped and 1x1 when
   created, stacked above its earlier siblings, a background that is a
   color or a painted RrAppearance below its children, and an optional
   border around the outside. */

#ifndef __ob_fwin_h
#define __ob_fwin_h

#include "misc.h"
#include "window.h"
#include "render/render.h"

#include <glib.h>

struct wlr_scene_tree;
struct wlr_scene_rect;
struct wlr_scene_buffer;

typedef struct _ObFWin ObFWin;

struct _ObFWin {
    ObNode node;
    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *rect;
    struct wlr_scene_buffer *buffer;
    /*! The border, drawn around the outside like an X window border */
    struct wlr_scene_rect *border;
    gint border_width;
    gint x, y, width, height;
    gboolean mapped;
    /*! The cursor shown while the pointer is over the window */
    ObCursor cursor;

    /*! X clips windows to their parents, these are for doing the same */
    ObFWin *parent;
    GList *children;
    /*! The size of the painted background */
    gint buf_width, buf_height;
};

/*! Create a window.  parent is the parent window, or NULL to put it in the
  scene tree parent_tree.  The window's node has the given type and
  owner. */
ObFWin* fwin_new(ObFWin *parent, struct wlr_scene_tree *parent_tree,
                 ObNodeType type, ObWindow *owner);
/*! Destroy a window, along with the scene nodes of its children (but not
  their ObFWin structs) */
void fwin_destroy(ObFWin *w);
/*! Free the struct of a window whose tree was destroyed with its parent */
void fwin_free(ObFWin *w);

void fwin_move(ObFWin *w, gint x, gint y);
void fwin_resize(ObFWin *w, gint width, gint height);
void fwin_move_resize(ObFWin *w, gint x, gint y, gint width, gint height);
void fwin_map(ObFWin *w);
void fwin_unmap(ObFWin *w);
/*! Like setting an X window's background pixel */
void fwin_set_color(ObFWin *w, const RrColor *c);
/*! Like RrPaint() into an X window */
void fwin_paint(ObFWin *w, RrAppearance *a, gint width, gint height);
/*! Show a painted surface as the window's background, the window takes
  the surface (which can be NULL) */
void fwin_set_surface(ObFWin *w, cairo_surface_t *s);
/*! The window has no background, it only takes input (an InputOnly
  window) */
void fwin_input_only(ObFWin *w);
/*! Like XSetWindowBorderWidth and XSetWindowBorder */
void fwin_set_border(ObFWin *w, gint width, const RrColor *c);
/*! Raise the window above its siblings */
void fwin_raise(ObFWin *w);

#endif
