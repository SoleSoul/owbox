/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   server.h for owbox
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

/* The Wayland side of owbox: the wlroots objects that stand in for the X
   server Openbox talks to.  Openbox's own modules (client, frame, focus,
   stacking, ...) keep their structure and use these through the helpers in
   the other Wayland-specific modules (output, seat, xdg, xwayland, layers). */

#ifndef __ob_server_h
#define __ob_server_h

#include "stacking.h"

#include <glib.h>
#include <wayland-server-core.h>

struct wlr_backend;
struct wlr_session;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_compositor;
struct wlr_scene;
struct wlr_scene_tree;
struct wlr_scene_rect;
struct wlr_scene_output_layout;
struct wlr_output_layout;
struct wlr_xdg_shell;
struct wlr_layer_shell_v1;
struct wlr_seat;
struct wlr_cursor;
struct wlr_xcursor_manager;
struct wlr_xwayland;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_ext_foreign_toplevel_list_v1;
struct wlr_ext_workspace_manager_v1;
struct wlr_output_manager_v1;
struct wlr_xdg_activation_v1;
struct wlr_session_lock_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_pointer_constraints_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_gestures_v1;
struct wlr_keyboard_shortcuts_inhibit_manager_v1;
struct wlr_cursor_shape_manager_v1;
struct wlr_xdg_decoration_manager_v1;
struct wlr_server_decoration_manager;
struct wlr_xdg_toplevel_icon_manager_v1;
struct wlr_presentation;
struct wlr_tearing_control_manager_v1;
struct wlr_output_power_manager_v1;
struct wlr_gamma_control_manager_v1;
struct wlr_xdg_output_manager_v1;
struct wlr_text_input_manager_v3;
struct wlr_input_method_manager_v2;
struct wlr_virtual_keyboard_manager_v1;
struct wlr_virtual_pointer_manager_v1;
struct wlr_data_device_manager;
struct wlr_xdg_system_bell_v1;

/*! The layer-shell layers, see wlr-layer-shell-unstable-v1 */
typedef enum {
    OB_LAYER_SHELL_BACKGROUND,
    OB_LAYER_SHELL_BOTTOM,
    OB_LAYER_SHELL_TOP,
    OB_LAYER_SHELL_OVERLAY,
    OB_NUM_LAYER_SHELL_LAYERS
} ObLayerShellLayer;

typedef struct _ObServer ObServer;

struct _ObServer {
    struct wl_display *display;
    struct wl_event_loop *loop;
    const gchar *socket;

    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_data_device_manager *data_device;

    struct wlr_output_layout *output_layout;
    struct wlr_xdg_output_manager_v1 *xdg_output;
    struct wlr_output_manager_v1 *output_manager;
    struct wlr_output_power_manager_v1 *output_power;
    struct wlr_gamma_control_manager_v1 *gamma_control;
    struct wl_list outputs; /* ObOutput::link */

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    /*! A solid rectangle under everything, standing in for the X root
      window when no wallpaper client is running */
    struct wlr_scene_rect *root_rect;
    /*! Trees for the layer shell, interleaved with Openbox's stacking
      layers, see server.c for the order */
    struct wlr_scene_tree *layer_tree[OB_NUM_LAYER_SHELL_LAYERS];
    /*! One tree per ObStackingLayer, holding the windows of that layer in
      their stacking order (topmost last) */
    struct wlr_scene_tree *stack_tree[OB_NUM_STACKING_LAYERS];
    /*! Holds client surfaces that are not in a frame (not managed) */
    struct wlr_scene_tree *unmanaged_tree;
    /*! Xwayland's override-redirect windows (menus, tooltips), above the
      managed windows */
    struct wlr_scene_tree *xwayland_tree;
    /*! Popups of layer shell surfaces, above all windows */
    struct wlr_scene_tree *layer_popup_tree;
    /*! Drag and drop icons */
    struct wlr_scene_tree *drag_tree;
    /*! The session lock (screen locker) surfaces, above everything */
    struct wlr_scene_tree *lock_tree;

    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration;
    struct wlr_server_decoration_manager *kde_decoration;
    struct wlr_xdg_toplevel_icon_manager_v1 *toplevel_icon;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wlr_xwayland *xwayland;

    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor_manager;
    struct wlr_cursor_shape_manager_v1 *cursor_shape;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_relative_pointer_manager_v1 *relative_pointer;
    struct wlr_pointer_gestures_v1 *pointer_gestures;
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer;
    struct wlr_text_input_manager_v3 *text_input;
    struct wlr_input_method_manager_v2 *input_method;

    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel;
    struct wlr_ext_foreign_toplevel_list_v1 *ext_foreign_toplevel;
    struct wlr_ext_workspace_manager_v1 *ext_workspace;
    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wlr_session_lock_manager_v1 *session_lock;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
    struct wlr_presentation *presentation;
    struct wlr_tearing_control_manager_v1 *tearing_control;
    struct wlr_xdg_system_bell_v1 *system_bell;
};

extern ObServer server;

/*! Create the display, backend, renderer and all the protocol globals.
  Exits with an error message when that is not possible. */
void server_init(void);
/*! Start the backend (outputs and input devices appear) and listen on a
  Wayland socket */
/*! Is owbox the session, running on the hardware (not nested or headless) */
gboolean server_is_session(void);
/*! Is owbox running in a window of another Wayland or X session */
gboolean server_is_nested(void);
void server_start(void);
void server_finish(void);

/*! Attach the Wayland event loop to the glib main context, so that Openbox's
  GMainLoop drives everything, its timeouts included */
void server_attach_to_main_context(GMainContext *context);

/*! Milliseconds of the monotonic clock, as used in input event timestamps */
guint32 server_now_msec(void);

#endif
