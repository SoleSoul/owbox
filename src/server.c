/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   server.c for owbox
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

#include "server.h"
#include "output.h"
#include "seat.h"
#include "xdg.h"
#include "layers.h"
#include "toplevel.h"
#include "xwayland.h"
#include "capture.h"
#include "config.h"
#include "openbox.h"
#include "debug.h"

#include <time.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/log.h>

ObServer server;

static struct wlr_linux_dmabuf_v1 *linux_dmabuf = NULL;

/* ---- the glib main loop drives the wayland event loop ---- */

typedef struct {
    GSource source;
    gpointer fd_tag;
} WaylandSource;

static gboolean wl_source_prepare(GSource *source, gint *timeout)
{
    /* run the wayland loop's idle callbacks that glib's sources (Openbox's
       timeouts) caused, like scheduling a frame to show a submenu.  they
       run in wl_event_loop_dispatch() only, which waits for the fd */
    wl_event_loop_dispatch_idle(server.loop);
    /* send out everything queued for the clients before sleeping */
    wl_display_flush_clients(server.display);
    *timeout = -1;
    return FALSE;
}

static gboolean wl_source_check(GSource *source)
{
    WaylandSource *s = (WaylandSource*)source;
    return g_source_query_unix_fd(source, s->fd_tag) != 0;
}

static gboolean wl_source_dispatch(GSource *source, GSourceFunc callback,
                                   gpointer data)
{
    wl_event_loop_dispatch(server.loop, 0);
    wl_display_flush_clients(server.display);
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs wl_source_funcs = {
    .prepare = wl_source_prepare,
    .check = wl_source_check,
    .dispatch = wl_source_dispatch,
};

void server_attach_to_main_context(GMainContext *context)
{
    WaylandSource *s;

    s = (WaylandSource*)g_source_new(&wl_source_funcs, sizeof(WaylandSource));
    s->fd_tag = g_source_add_unix_fd(&s->source,
                                     wl_event_loop_get_fd(server.loop),
                                     G_IO_IN | G_IO_ERR | G_IO_HUP);
    g_source_set_priority(&s->source, G_PRIORITY_HIGH);
    g_source_attach(&s->source, context);
    g_source_unref(&s->source);
}

guint32 server_now_msec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (guint32)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

/* ---- logging ---- */

static void wlr_log_handler(enum wlr_log_importance importance,
                            const char *fmt, va_list args)
{
    gchar *msg = g_strdup_vprintf(fmt, args);
    if (importance == WLR_ERROR)
        g_message("wlroots: %s", msg);
    else
        ob_debug("wlroots: %s", msg);
    g_free(msg);
}

/* ---- setup ---- */

/* Only let sandboxed clients (security-context) use the protocols that are
   safe for them */
static bool filter_global(const struct wl_client *client,
                          const struct wl_global *global, void *data)
{
    const struct wlr_security_context_v1_state *ctx =
        wlr_security_context_manager_v1_lookup_client(data,
                                                      (struct wl_client*)client);
    const struct wl_interface *iface;
    static const gchar *privileged[] = {
        "zwlr_layer_shell_v1", "zwlr_screencopy_manager_v1",
        "ext_image_copy_capture_manager_v1", "zwlr_export_dmabuf_manager_v1",
        "ext_foreign_toplevel_image_capture_source_manager_v1",
        "zwlr_gamma_control_manager_v1", "zwlr_output_manager_v1",
        "zwlr_output_power_manager_v1", "zwlr_data_control_manager_v1",
        "ext_data_control_manager_v1", "zwlr_foreign_toplevel_manager_v1",
        "ext_foreign_toplevel_list_v1", "ext_workspace_manager_v1",
        "zwp_virtual_keyboard_manager_v1", "zwlr_virtual_pointer_manager_v1",
        "ext_session_lock_manager_v1", "zwp_input_method_manager_v2",
        "wp_security_context_manager_v1", NULL
    };
    gint i;

    if (!xwayland_global_allowed(client, global))
        return false;
    if (!ctx) return true;
    iface = wl_global_get_interface(global);
    for (i = 0; privileged[i]; ++i)
        if (!g_strcmp0(iface->name, privileged[i]))
            return false;
    return true;
}

void server_init(void)
{
    gint i;
    struct wlr_security_context_manager_v1 *security;

    wlr_log_init(ob_debug_wlroots() ? WLR_DEBUG : WLR_ERROR,
                 wlr_log_handler);

    server.display = wl_display_create();
    server.loop = wl_display_get_event_loop(server.display);

    server.backend = wlr_backend_autocreate(server.loop, &server.session);
    if (!server.backend)
        ob_exit_with_error("Unable to create the wlroots backend.");

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer)
        ob_exit_with_error("Unable to create the wlroots renderer.");
    wlr_renderer_init_wl_shm(server.renderer, server.display);

    if (wlr_renderer_get_texture_formats(server.renderer,
                                         WLR_BUFFER_CAP_DMABUF))
    {
        wlr_drm_create(server.display, server.renderer);
        linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(
            server.display, 5, server.renderer);
    }

    server.allocator = wlr_allocator_autocreate(server.backend,
                                                server.renderer);
    if (!server.allocator)
        ob_exit_with_error("Unable to create the wlroots allocator.");

    server.compositor = wlr_compositor_create(server.display, 6,
                                              server.renderer);
    wlr_subcompositor_create(server.display);
    server.data_device = wlr_data_device_manager_create(server.display);
    wlr_primary_selection_v1_device_manager_create(server.display);
    wlr_data_control_manager_v1_create(server.display);
    wlr_ext_data_control_manager_v1_create(server.display, 1);

    server.output_layout = wlr_output_layout_create(server.display);
    server.xdg_output = wlr_xdg_output_manager_v1_create(server.display,
                                                         server.output_layout);

    /* the scene graph, from the bottom up:
       the root color, layer shell background and bottom, Openbox's stacking
       layers up to above, the shown panels, layer shell top, fullscreen
       windows (below the shown panels while they are), layer shell
       overlay, X override-redirect windows (menus, tooltips), Openbox's
       internal windows (menus, popups), layer shell
       popups, drag icons, and the lock screen */
    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene,
                                                         server.output_layout);
    {
        float c[4] = { config_root_color[0] / 255.0f,
                       config_root_color[1] / 255.0f,
                       config_root_color[2] / 255.0f, 1.0f };
        server.root_rect = wlr_scene_rect_create(&server.scene->tree, 0, 0, c);
    }
    server.unmanaged_tree = wlr_scene_tree_create(&server.scene->tree);
    wlr_scene_node_set_enabled(&server.unmanaged_tree->node, FALSE);
    server.layer_tree[OB_LAYER_SHELL_BACKGROUND] =
        wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[OB_LAYER_SHELL_BOTTOM] =
        wlr_scene_tree_create(&server.scene->tree);
    for (i = OB_STACKING_LAYER_INVALID; i <= OB_STACKING_LAYER_ABOVE; ++i)
        server.stack_tree[i] = wlr_scene_tree_create(&server.scene->tree);
    /* the dim over the windows and the panels above it, while the panels
       are shown (panels.c) */
    server.panels_tree = wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[OB_LAYER_SHELL_TOP] =
        wlr_scene_tree_create(&server.scene->tree);
    server.stack_tree[OB_STACKING_LAYER_FULLSCREEN] =
        wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[OB_LAYER_SHELL_OVERLAY] =
        wlr_scene_tree_create(&server.scene->tree);
    server.xwayland_tree = wlr_scene_tree_create(&server.scene->tree);
    server.stack_tree[OB_STACKING_LAYER_INTERNAL] =
        wlr_scene_tree_create(&server.scene->tree);
    server.layer_popup_tree = wlr_scene_tree_create(&server.scene->tree);
    server.drag_tree = wlr_scene_tree_create(&server.scene->tree);
    server.lock_tree = wlr_scene_tree_create(&server.scene->tree);

    if (linux_dmabuf)
        wlr_scene_set_linux_dmabuf_v1(server.scene, linux_dmabuf);

    server.gamma_control = wlr_gamma_control_manager_v1_create(server.display);
    wlr_scene_set_gamma_control_manager_v1(server.scene,
                                           server.gamma_control);

    /* the rest of the protocols that need no help from owbox */
    wlr_viewporter_create(server.display);
    wlr_single_pixel_buffer_manager_v1_create(server.display);
    wlr_fractional_scale_manager_v1_create(server.display, 1);
    server.presentation = wlr_presentation_create(server.display,
                                                  server.backend, 2);
    wlr_screencopy_manager_v1_create(server.display);
    wlr_export_dmabuf_manager_v1_create(server.display);
    capture_init();
    wlr_alpha_modifier_v1_create(server.display);
    wlr_content_type_manager_v1_create(server.display, 1);
    server.tearing_control =
        wlr_tearing_control_manager_v1_create(server.display, 1);
    server.system_bell = wlr_xdg_system_bell_v1_create(server.display, 1);
    {
        struct wlr_xdg_foreign_registry *reg =
            wlr_xdg_foreign_registry_create(server.display);
        wlr_xdg_foreign_v1_create(server.display, reg);
        wlr_xdg_foreign_v2_create(server.display, reg);
    }

    output_init();
    seat_init();
    xdg_init();
    layers_init();
    toplevel_init();
    xwayland_init();

    security = wlr_security_context_manager_v1_create(server.display);
    wl_display_set_global_filter(server.display, filter_global, security);
}

static void find_drm(struct wlr_backend *backend, void *data)
{
    if (wlr_backend_is_drm(backend))
        *(gboolean*)data = TRUE;
}

static void find_nested(struct wlr_backend *backend, void *data)
{
    if (wlr_backend_is_wl(backend) || wlr_backend_is_x11(backend))
        *(gboolean*)data = TRUE;
}

gboolean server_is_nested(void)
{
    gboolean nested = FALSE;
    if (wlr_backend_is_multi(server.backend))
        wlr_multi_for_each_backend(server.backend, find_nested, &nested);
    else
        find_nested(server.backend, &nested);
    return nested;
}

gboolean server_is_session(void)
{
    gboolean drm = FALSE;
    if (wlr_backend_is_multi(server.backend))
        wlr_multi_for_each_backend(server.backend, find_drm, &drm);
    else
        drm = wlr_backend_is_drm(server.backend);
    return drm;
}

void server_start(void)
{
    server.socket = wl_display_add_socket_auto(server.display);
    if (!server.socket)
        ob_exit_with_error("Unable to open a Wayland socket.");

    if (!wlr_backend_start(server.backend))
        ob_exit_with_error("Unable to start the wlroots backend.");

    g_setenv("WAYLAND_DISPLAY", server.socket, TRUE);
    ob_debug("Running on WAYLAND_DISPLAY=%s", server.socket);
    xwayland_start();
}

void server_finish(void)
{
    xwayland_finish();
    wl_display_destroy_clients(server.display);
    capture_finish();
    toplevel_finish();
    layers_finish();
    xdg_finish();
    seat_finish();
    output_finish();
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.display);
}
