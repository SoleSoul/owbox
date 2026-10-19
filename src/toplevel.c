/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   toplevel.c for owbox
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

#include "toplevel.h"
#include "client.h"
#include "frame.h"
#include "server.h"
#include "screen.h"
#include "focus.h"
#include "output.h"
#include "openbox.h"
#include "event.h"
#include "icons.h"
#include "xdg.h"
#include "debug.h"

#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

/* from the ext-workspace-v1 protocol */
#define GROUP_CAP_CREATE_WORKSPACE 1
#define WORKSPACE_CAP_ACTIVATE     1
#define WORKSPACE_CAP_REMOVE       4
#define WORKSPACE_CAP_ASSIGN       8

typedef struct {
    ObClient *client;
    struct wlr_foreign_toplevel_handle_v1 *wlr;
    struct wlr_ext_foreign_toplevel_handle_v1 *ext;
    /*! The outputs the window is on, for output_enter/leave */
    GSList *outputs;
    /*! The window's contents for screen sharing (ext-image-capture-source),
      made when a client asks for it */
    struct wlr_ext_image_capture_source_v1 *capture;
    struct wl_listener capture_destroy;

    struct wl_listener request_maximize;
    struct wl_listener request_minimize;
    struct wl_listener request_activate;
    struct wl_listener request_fullscreen;
    struct wl_listener request_close;
    struct wl_listener set_rectangle;
} ObToplevelHandles;

static GHashTable *handles = NULL; /* ObClient* -> ObToplevelHandles* */

static struct wlr_ext_workspace_group_handle_v1 *ws_group = NULL;
static GPtrArray *workspaces = NULL; /* wlr_ext_workspace_handle_v1* */

static struct wl_listener ws_commit;
static struct wl_listener activation_request;
static struct wl_listener new_dialog;
static struct wl_listener new_capture_request;

static struct wlr_xdg_wm_dialog_v1 *wm_dialog;

/* ---- windows ---- */

static void handle_request_maximize(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, request_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *ev = data;
    client_maximize(h->client, ev->maximized, 0);
}

static void handle_request_minimize(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, request_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *ev = data;
    client_iconify(h->client, ev->minimized, TRUE, FALSE);
}

static void handle_request_activate(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, request_activate);
    /* like a _NET_ACTIVE_WINDOW message from a pager */
    client_activate(h->client, TRUE, FALSE, TRUE, TRUE, TRUE);
}

static void handle_request_fullscreen(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, request_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *ev = data;
    client_fullscreen(h->client, ev->fullscreen);
}

static void handle_request_close(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, request_close);
    client_close(h->client);
}

static void handle_set_rectangle(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, set_rectangle);
    struct wlr_foreign_toplevel_handle_v1_set_rectangle_event *ev = data;
    struct wlr_scene_buffer *b;
    gint sx = 0, sy = 0;

    /* the rectangle is relative to the taskbar's surface, this is
       _NET_WM_ICON_GEOMETRY, which the iconify animation goes to */
    if (ev->surface &&
        (b = window_scene_buffer_for_surface(ev->surface)))
        wlr_scene_node_coords(&b->node, &sx, &sy);
    RECT_SET(h->client->icon_geometry, sx + ev->x, sy + ev->y,
             MAX(ev->width, 0), MAX(ev->height, 0));
}

static void update_outputs(ObToplevelHandles *h)
{
    ObClient *c = h->client;
    guint i;
    GSList *now = NULL, *it;

    if (c->frame && c->frame->visible)
        for (i = 0; i < output_count(); ++i) {
            ObOutput *o = output_nth(i);
            if (RECT_INTERSECTS_RECT(o->area, c->frame->area))
                now = g_slist_prepend(now, o->wlr_output);
        }

    for (it = now; it; it = g_slist_next(it))
        if (!g_slist_find(h->outputs, it->data))
            wlr_foreign_toplevel_handle_v1_output_enter(h->wlr, it->data);
    for (it = h->outputs; it; it = g_slist_next(it))
        if (!g_slist_find(now, it->data))
            wlr_foreign_toplevel_handle_v1_output_leave(h->wlr, it->data);
    g_slist_free(h->outputs);
    h->outputs = now;
}

void toplevel_new(ObClient *c)
{
    ObToplevelHandles *h;
    struct wlr_ext_foreign_toplevel_handle_v1_state state = {
        .title = c->title, .app_id = c->class,
    };

    h = g_new0(ObToplevelHandles, 1);
    h->client = c;
    h->wlr = wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel);
    h->ext = wlr_ext_foreign_toplevel_handle_v1_create(
        server.ext_foreign_toplevel, &state);
    h->ext->data = h;
    g_hash_table_insert(handles, c, h);

#define LISTEN(sig, l, f) do { h->l.notify = (f); wl_signal_add((sig), &h->l); } while (0)
    LISTEN(&h->wlr->events.request_maximize, request_maximize,
           handle_request_maximize);
    LISTEN(&h->wlr->events.request_minimize, request_minimize,
           handle_request_minimize);
    LISTEN(&h->wlr->events.request_activate, request_activate,
           handle_request_activate);
    LISTEN(&h->wlr->events.request_fullscreen, request_fullscreen,
           handle_request_fullscreen);
    LISTEN(&h->wlr->events.request_close, request_close,
           handle_request_close);
    LISTEN(&h->wlr->events.set_rectangle, set_rectangle,
           handle_set_rectangle);
#undef LISTEN

    toplevel_update_title(c);
    toplevel_update_app_id(c);
    toplevel_update_state(c);
}

void toplevel_destroy(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    if (!h) return;

    wl_list_remove(&h->request_maximize.link);
    wl_list_remove(&h->request_minimize.link);
    wl_list_remove(&h->request_activate.link);
    wl_list_remove(&h->request_fullscreen.link);
    wl_list_remove(&h->request_close.link);
    wl_list_remove(&h->set_rectangle.link);
    if (h->capture)
        wl_list_remove(&h->capture_destroy.link);
    wlr_foreign_toplevel_handle_v1_destroy(h->wlr);
    wlr_ext_foreign_toplevel_handle_v1_destroy(h->ext);
    g_slist_free(h->outputs);
    g_hash_table_remove(handles, c);
    g_free(h);
}

void toplevel_update_title(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    struct wlr_ext_foreign_toplevel_handle_v1_state state;

    if (!h) return;
    wlr_foreign_toplevel_handle_v1_set_title(h->wlr, c->title);
    state.title = c->title;
    state.app_id = c->class;
    wlr_ext_foreign_toplevel_handle_v1_update_state(h->ext, &state);
}

void toplevel_update_app_id(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    struct wlr_ext_foreign_toplevel_handle_v1_state state;

    if (!h) return;
    wlr_foreign_toplevel_handle_v1_set_app_id(h->wlr, c->class);
    state.title = c->title;
    state.app_id = c->class;
    wlr_ext_foreign_toplevel_handle_v1_update_state(h->ext, &state);
}

void toplevel_update_state(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    ObClient *p;

    if (!h) return;
    wlr_foreign_toplevel_handle_v1_set_maximized(h->wlr,
                                                 c->max_horz && c->max_vert);
    wlr_foreign_toplevel_handle_v1_set_minimized(h->wlr, c->iconic);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(h->wlr, c->fullscreen);
    wlr_foreign_toplevel_handle_v1_set_activated(h->wlr, client_focused(c));
    p = client_direct_parent(c);
    wlr_foreign_toplevel_handle_v1_set_parent(h->wlr,
        p && g_hash_table_lookup(handles, p) ?
        ((ObToplevelHandles*)g_hash_table_lookup(handles, p))->wlr : NULL);
    update_outputs(h);
}

void toplevel_update_geometry(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    if (h) update_outputs(h);
}

void toplevel_update_desktop(ObClient *c)
{
    toplevel_update_geometry(c);
}

void toplevel_update_focus(ObClient *old, ObClient *new_)
{
    ObToplevelHandles *h;
    if (old && (h = g_hash_table_lookup(handles, old)))
        wlr_foreign_toplevel_handle_v1_set_activated(h->wlr, FALSE);
    if (new_ && (h = g_hash_table_lookup(handles, new_)))
        wlr_foreign_toplevel_handle_v1_set_activated(h->wlr, TRUE);
}

void toplevel_update_stacking(void)
{
    GHashTableIter it;
    gpointer key, value;

    /* windows may have been shown/hidden on outputs */
    if (!handles) return;
    g_hash_table_iter_init(&it, handles);
    while (g_hash_table_iter_next(&it, &key, &value))
        update_outputs(value);
}

/* ---- desktops as workspaces ---- */

static void update_workspace_outputs(void)
{
    guint i;
    if (!ws_group) return;
    for (i = 0; i < output_count(); ++i)
        wlr_ext_workspace_group_handle_v1_output_enter(
            ws_group, output_nth(i)->wlr_output);
}

void toplevel_update_desktops(void)
{
    guint i;

    if (!workspaces) return;

    /* match the number of desktops */
    while (workspaces->len > screen_num_desktops) {
        wlr_ext_workspace_handle_v1_destroy(
            g_ptr_array_index(workspaces, workspaces->len - 1));
        g_ptr_array_set_size(workspaces, workspaces->len - 1);
    }
    while (workspaces->len < screen_num_desktops) {
        gchar *id = g_strdup_printf("desktop-%u", workspaces->len + 1);
        struct wlr_ext_workspace_handle_v1 *ws =
            wlr_ext_workspace_handle_v1_create(
                server.ext_workspace, id,
                WORKSPACE_CAP_ACTIVATE | WORKSPACE_CAP_REMOVE |
                WORKSPACE_CAP_ASSIGN);
        g_free(id);
        wlr_ext_workspace_handle_v1_set_group(ws, ws_group);
        g_ptr_array_add(workspaces, ws);
    }

    update_workspace_outputs();

    for (i = 0; i < workspaces->len; ++i) {
        struct wlr_ext_workspace_handle_v1 *ws =
            g_ptr_array_index(workspaces, i);
        guint32 coords[2];
        guint row, col;
        gboolean urgent = FALSE;
        GList *it;

        if (screen_desktop_names && screen_desktop_names[i])
            wlr_ext_workspace_handle_v1_set_name(ws, screen_desktop_names[i]);

        /* where the desktop is in the desktop layout */
        if (screen_desktop_layout.orientation == OB_ORIENTATION_HORZ) {
            row = i / MAX(screen_desktop_layout.columns, 1);
            col = i % MAX(screen_desktop_layout.columns, 1);
        } else {
            col = i / MAX(screen_desktop_layout.rows, 1);
            row = i % MAX(screen_desktop_layout.rows, 1);
        }
        coords[0] = col;
        coords[1] = row;
        wlr_ext_workspace_handle_v1_set_coordinates(ws, coords, 2);

        wlr_ext_workspace_handle_v1_set_active(ws, i == screen_desktop);
        for (it = client_list; it; it = g_list_next(it)) {
            ObClient *c = it->data;
            if (c->desktop == i && c->demands_attention)
                urgent = TRUE;
        }
        wlr_ext_workspace_handle_v1_set_urgent(ws, urgent);
    }
}

static gint workspace_index(struct wlr_ext_workspace_handle_v1 *ws)
{
    guint i;
    for (i = 0; ws && i < workspaces->len; ++i)
        if (g_ptr_array_index(workspaces, i) == ws)
            return i;
    return -1;
}

static void handle_ws_commit(struct wl_listener *l, void *data)
{
    struct wlr_ext_workspace_v1_commit_event *ev = data;
    struct wlr_ext_workspace_v1_request *req;
    gint d;

    wl_list_for_each(req, ev->requests, link) {
        switch (req->type) {
        case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE:
            d = workspace_index(req->activate.workspace);
            if (d >= 0)
                screen_set_desktop(d, TRUE);
            break;
        case WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE:
            screen_add_desktop(FALSE);
            if (req->create_workspace.name)
                screen_set_desktop_name(screen_num_desktops - 1,
                                        req->create_workspace.name);
            break;
        case WLR_EXT_WORKSPACE_V1_REQUEST_REMOVE:
            d = workspace_index(req->remove.workspace);
            if (d >= 0 && screen_num_desktops > 1) {
                guint cur = screen_desktop;
                screen_set_desktop(d, FALSE);
                screen_remove_desktop(TRUE);
                if (cur < screen_num_desktops && cur != (guint)d)
                    screen_set_desktop(cur > (guint)d ? cur - 1 : cur,
                                       TRUE);
            }
            break;
        default:
            break;
        }
    }
    toplevel_update_desktops();
}

/* ---- activation (startup notification) ---- */

static void handle_activation_request(struct wl_listener *l, void *data)
{
    struct wlr_xdg_activation_v1_request_activate_event *ev = data;
    ObClient *c = client_from_surface(ev->surface);
    gboolean user;

    if (!c || !c->managed) return;

    /* a token made from user input in the focused window is like a user
       request, otherwise the window can only ask for attention */
    user = ev->token->seat && ev->token->surface &&
        focus_client && client_from_surface(ev->token->surface) ==
        focus_client;
    if (!ev->token->surface && ev->token->seat)
        /* made by a launcher (owbox itself, or a panel) */
        user = TRUE;

    client_activate(c, TRUE, FALSE, TRUE, TRUE, user);
}

const gchar* toplevel_launch_token(const gchar *app_id)
{
    struct wlr_xdg_activation_token_v1 *token;

    token = wlr_xdg_activation_token_v1_create(server.xdg_activation);
    if (!token) return NULL;
    /* the token comes from the user's input through owbox */
    token->seat = server.seat;
    token->serial = 0;
    if (app_id)
        token->app_id = g_strdup(app_id);
    return wlr_xdg_activation_token_v1_get_name(token);
}

guint32 toplevel_activation_time(ObClient *c)
{
    return 0;
}

gboolean toplevel_activation_desktop(ObClient *c, guint *desktop)
{
    return FALSE;
}

/* ---- dialogs ---- */

typedef struct {
    struct wlr_xdg_dialog_v1 *dialog;
    struct wl_listener set_modal;
    struct wl_listener destroy;
} ObDialog;

static void dialog_set_modal(struct wl_listener *l, void *data)
{
    ObDialog *d = wl_container_of(l, d, set_modal);
    ObClient *c = client_from_surface(d->dialog->xdg_toplevel->base->surface);
    if (c && c->managed && c->modal != d->dialog->modal) {
        c->modal = d->dialog->modal;
        stacking_raise(CLIENT_AS_WINDOW(c));
        if (focus_client && client_focus_target(focus_client) != focus_client)
            client_focus(focus_client);
    }
}

static void dialog_destroy(struct wl_listener *l, void *data)
{
    ObDialog *d = wl_container_of(l, d, destroy);
    wl_list_remove(&d->set_modal.link);
    wl_list_remove(&d->destroy.link);
    g_free(d);
}

static void handle_new_dialog(struct wl_listener *l, void *data)
{
    struct wlr_xdg_dialog_v1 *dialog = data;
    ObDialog *d = g_new0(ObDialog, 1);
    d->dialog = dialog;
    d->set_modal.notify = dialog_set_modal;
    wl_signal_add(&dialog->events.set_modal, &d->set_modal);
    d->destroy.notify = dialog_destroy;
    wl_signal_add(&dialog->events.destroy, &d->destroy);
}

gboolean toplevel_is_modal(ObClient *c)
{
    struct wlr_xdg_dialog_v1 *d;
    struct wlr_surface *s = c->impl->surface(c);
    struct wlr_xdg_toplevel *t;

    if (!s || !(t = wlr_xdg_toplevel_try_from_wlr_surface(s)))
        return FALSE;
    d = wlr_xdg_dialog_v1_try_from_wlr_xdg_toplevel(t);
    return d && d->modal;
}

RrImage* toplevel_find_icon(ObClient *c)
{
    return icons_for_client(c);
}

/* ---- setup ---- */

const gchar* toplevel_identifier(ObClient *c)
{
    ObToplevelHandles *h = g_hash_table_lookup(handles, c);
    return h ? h->ext->identifier : NULL;
}

/* ---- window capture (screen sharing a window) ---- */

static void handle_capture_destroy(struct wl_listener *l, void *data)
{
    ObToplevelHandles *h = wl_container_of(l, h, capture_destroy);
    wl_list_remove(&h->capture_destroy.link);
    h->capture = NULL;
}

static void handle_new_capture_request(struct wl_listener *l, void *data)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request
        *req = data;
    ObToplevelHandles *h = req->toplevel_handle->data;

    if (!h) return;
    if (!h->capture) {
        /* the client's surfaces with their popups, without the frame */
        h->capture = wlr_ext_image_capture_source_v1_create_with_scene_node(
            &h->client->surface_tree->node,
            wl_display_get_event_loop(server.display),
            server.allocator, server.renderer);
        if (!h->capture) return;
        h->capture_destroy.notify = handle_capture_destroy;
        wl_signal_add(&h->capture->events.destroy, &h->capture_destroy);
    }
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
        req, h->capture);
}

void toplevel_init(void)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *capture;

    handles = g_hash_table_new(g_direct_hash, g_direct_equal);

    server.foreign_toplevel =
        wlr_foreign_toplevel_manager_v1_create(server.display);
    server.ext_foreign_toplevel =
        wlr_ext_foreign_toplevel_list_v1_create(server.display, 1);

    server.ext_workspace =
        wlr_ext_workspace_manager_v1_create(server.display, 1);
    /* Openbox's desktops span all the monitors, so there is one group */
    ws_group = wlr_ext_workspace_group_handle_v1_create(
        server.ext_workspace, GROUP_CAP_CREATE_WORKSPACE);
    workspaces = g_ptr_array_new();
    ws_commit.notify = handle_ws_commit;
    wl_signal_add(&server.ext_workspace->events.commit, &ws_commit);

    server.xdg_activation = wlr_xdg_activation_v1_create(server.display);
    activation_request.notify = handle_activation_request;
    wl_signal_add(&server.xdg_activation->events.request_activate,
                  &activation_request);

    wm_dialog = wlr_xdg_wm_dialog_v1_create(server.display, 1);
    new_dialog.notify = handle_new_dialog;
    wl_signal_add(&wm_dialog->events.new_dialog, &new_dialog);

    capture = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(
        server.display, 1);
    new_capture_request.notify = handle_new_capture_request;
    wl_signal_add(&capture->events.new_request, &new_capture_request);
}

void toplevel_finish(void)
{
    wl_list_remove(&ws_commit.link);
    wl_list_remove(&activation_request.link);
    wl_list_remove(&new_dialog.link);
    wl_list_remove(&new_capture_request.link);
    g_ptr_array_free(workspaces, TRUE);
    workspaces = NULL;
    g_hash_table_destroy(handles);
    handles = NULL;
}
