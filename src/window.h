/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   window.h for owbox (ported from the Openbox window manager)
   Copyright (c) 2003-2007   Dana Jansens
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

#ifndef __window_h
#define __window_h

#include "stacking.h"

#include <glib.h>

struct wlr_scene_node;
struct wlr_scene_tree;
struct wlr_surface;
struct wlr_scene_buffer;

typedef struct _ObWindow         ObWindow;
typedef struct _ObInternalWindow ObInternalWindow;
typedef struct _ObNode           ObNode;

typedef enum {
    OB_WINDOW_CLASS_MENUFRAME,
    OB_WINDOW_CLASS_DOCK,
    OB_WINDOW_CLASS_CLIENT,
    OB_WINDOW_CLASS_INTERNAL,
    OB_WINDOW_CLASS_PROMPT,
    /*! A layer-shell surface (panels, wallpapers, docks) */
    OB_WINDOW_CLASS_LAYER,
    /*! An unmanaged (override-redirect) Xwayland window */
    OB_WINDOW_CLASS_UNMANAGED
} ObWindowClass;

/* In order to be an ObWindow, you need to make this struct the top of your
   struct */
struct _ObWindow {
    ObWindowClass type;
};

#define WINDOW_IS_MENUFRAME(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_MENUFRAME)
#define WINDOW_IS_DOCK(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_DOCK)
#define WINDOW_IS_CLIENT(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_CLIENT)
#define WINDOW_IS_INTERNAL(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_INTERNAL)
#define WINDOW_IS_PROMPT(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_PROMPT)
#define WINDOW_IS_LAYER(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_LAYER)
#define WINDOW_IS_UNMANAGED(win) \
    (((ObWindow*)win)->type == OB_WINDOW_CLASS_UNMANAGED)

struct _ObMenu;
struct _ObDock;
struct _ObDockApp;
struct _ObClient;
struct _ObPrompt;

#define WINDOW_AS_MENUFRAME(win) ((struct _ObMenuFrame*)win)
#define WINDOW_AS_DOCK(win) ((struct _ObDock*)win)
#define WINDOW_AS_CLIENT(win) ((struct _ObClient*)win)
#define WINDOW_AS_INTERNAL(win) ((struct _ObInternalWindow*)win)
#define WINDOW_AS_PROMPT(win) ((struct _ObPrompt*)win)
#define MENUFRAME_AS_WINDOW(menu) ((ObWindow*)menu)
#define DOCK_AS_WINDOW(dock) ((ObWindow*)dock)
#define CLIENT_AS_WINDOW(client) ((ObWindow*)client)
#define INTERNAL_AS_WINDOW(intern) ((ObWindow*)intern)
#define PROMPT_AS_WINDOW(prompt) ((ObWindow*)prompt)

/*! What a scene node stands for.  Openbox looks up which ObWindow an X
  window belongs to (window_find); owbox hangs an ObNode off the data pointer
  of the scene nodes instead, and finds it by walking up the scene graph. */
typedef enum {
    /*! The scene tree of a whole ObWindow (a client's surfaces, a menu, an
      internal popup) */
    OB_NODE_WINDOW,
    /*! One of the element "windows" of a client's frame, see frame.h */
    OB_NODE_FRAME_PART,
    /*! An entry in a menu frame */
    OB_NODE_MENU_ENTRY,
} ObNodeType;

struct _ObNode {
    ObNodeType type;
    ObWindow *owner;
};

void ob_node_attach(ObNode *node, struct wlr_scene_node *scene_node);

/*! Find the ObNode for a scene node, looking at its parents if needed */
ObNode* ob_node_find(struct wlr_scene_node *scene_node);

/*! Finds what is under the given layout coordinates.  Returns the ObNode
  of the thing (or NULL if it is the bare root), and the surface under the
  point if there is one, with surface-local coordinates. */
ObNode* window_at(double lx, double ly, struct wlr_surface **surface,
                  double *sx, double *sy);

/*! The scene buffer that shows a surface, or NULL */
struct wlr_scene_buffer* window_scene_buffer_for_surface(
    struct wlr_surface *surface);

/*! Openbox's own windows (popups, prompts) start with these members */
struct _ObInternalWindow {
    ObWindowClass type;
    struct _ObFWin *window;
};

void window_startup (gboolean reconfig);
void window_shutdown(gboolean reconfig);

/*! The scene tree that holds the whole window, the equivalent of the X
  window that Openbox restacks */
struct wlr_scene_tree* window_top(ObWindow *self);
ObStackingLayer window_layer(ObWindow *self);

/*! Unmanage all the windows, at exit */
void window_unmanage_all(void);

#endif
