/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xdg.c for owbox
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

/* xdg-shell windows as Openbox clients.  This is the Wayland counterpart of
   the X property reading and client messages in Openbox's client.c and
   event.c. */

#include "xdg.h"
#include "client.h"
#include "frame.h"
#include "server.h"
#include "config.h"
#include "screen.h"
#include "output.h"
#include "openbox.h"
#include "focus.h"
#include "moveresize.h"
#include "menu.h"
#include "seat.h"
#include "toplevel.h"
#include "debug.h"
#include "layers.h"
#include "xdg-shell-protocol.h"

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

typedef struct {
    ObClient *client;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *tree;

    /*! The serial of the last configure that changed the size */
    guint32 size_serial;
    gboolean size_requested;

    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener decoration_request_mode;
    struct wl_listener decoration_destroy;

    /*! The minimum and maximum size last seen, to notice changes */
    gint min_w, min_h, max_w, max_h;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener request_show_window_menu;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener set_parent;
} ObXdgToplevel;

typedef struct {
    struct wlr_xdg_popup *popup;
    struct wl_listener commit;
    struct wl_listener destroy;
} ObXdgPopup;

typedef struct {
    struct wlr_server_decoration *deco;
    struct wl_listener mode;
    struct wl_listener destroy;
} ObKdeDecoration;

static struct wl_listener new_toplevel;
static struct wl_listener new_popup;
static struct wl_listener new_decoration;
static struct wl_listener new_kde_decoration;
static GList *kde_decorations = NULL;

#define XDG(c) ((ObXdgToplevel*)(c)->impl_data)

/* ---- decorations ---- */

static gboolean kde_wants_server(struct wlr_surface *surface)
{
    GList *it;
    for (it = kde_decorations; it; it = g_list_next(it)) {
        ObKdeDecoration *d = it->data;
        if (d->deco->surface == surface)
            return d->deco->mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
    }
    return FALSE;
}

/*! Will the client let owbox decorate it, or does it draw its own
  decorations */
static gboolean wants_server_decorations(ObXdgToplevel *x)
{
    if (config_decoration_policy == OB_DECORATION_POLICY_SERVER)
        return TRUE;
    if (config_decoration_policy == OB_DECORATION_POLICY_CLIENT)
        return FALSE;
    if (x->decoration)
        return x->decoration->current.mode !=
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    return kde_wants_server(x->toplevel->base->surface);
}

static void decoration_set_mode(ObXdgToplevel *x)
{
    enum wlr_xdg_toplevel_decoration_v1_mode mode;

    if (!x->decoration || !x->toplevel->base->initialized) return;

    switch (config_decoration_policy) {
    case OB_DECORATION_POLICY_CLIENT:
        mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        break;
    case OB_DECORATION_POLICY_SERVER:
        mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        break;
    default:
        /* Openbox decorates everything that doesn't insist otherwise */
        mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        if (x->decoration->requested_mode ==
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE &&
            !config_force_server_decorations)
            mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        break;
    }
    wlr_xdg_toplevel_decoration_v1_set_mode(x->decoration, mode);
}

static void decorations_changed(ObXdgToplevel *x)
{
    ObClient *c = x->client;
    if (!c->managed) return;
    client_get_type_and_transientness(c);
    client_setup_decor_and_functions(c, TRUE);
}

static void decoration_handle_request_mode(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, decoration_request_mode);
    decoration_set_mode(x);
}

static void decoration_handle_destroy(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, decoration_destroy);
    wl_list_remove(&x->decoration_request_mode.link);
    wl_list_remove(&x->decoration_destroy.link);
    x->decoration = NULL;
    decorations_changed(x);
}

static void handle_new_decoration(struct wl_listener *l, void *data)
{
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    ObXdgToplevel *x;
    ObClient *c;
    ObNode *node;

    /* the toplevel's scene tree is its ObClient's node */
    node = deco->toplevel->base->data ?
        ((struct wlr_scene_tree*)deco->toplevel->base->data)->node.data :
        NULL;
    if (!node || !node->owner || !WINDOW_IS_CLIENT(node->owner)) return;
    c = WINDOW_AS_CLIENT(node->owner);
    x = XDG(c);

    x->decoration = deco;
    x->decoration_request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&deco->events.request_mode, &x->decoration_request_mode);
    x->decoration_destroy.notify = decoration_handle_destroy;
    wl_signal_add(&deco->events.destroy, &x->decoration_destroy);
    decoration_set_mode(x);
}

static void kde_decoration_mode(struct wl_listener *l, void *data)
{
    ObKdeDecoration *d = wl_container_of(l, d, mode);
    ObClient *c = client_from_surface(d->deco->surface);
    if (c && c->impl == &xdg_client_impl)
        decorations_changed(XDG(c));
}

static void kde_decoration_destroy(struct wl_listener *l, void *data)
{
    ObKdeDecoration *d = wl_container_of(l, d, destroy);
    kde_decorations = g_list_remove(kde_decorations, d);
    wl_list_remove(&d->mode.link);
    wl_list_remove(&d->destroy.link);
    g_free(d);
}

static void handle_new_kde_decoration(struct wl_listener *l, void *data)
{
    struct wlr_server_decoration *deco = data;
    ObKdeDecoration *d = g_new0(ObKdeDecoration, 1);
    d->deco = deco;
    d->mode.notify = kde_decoration_mode;
    wl_signal_add(&deco->events.mode, &d->mode);
    d->destroy.notify = kde_decoration_destroy;
    wl_signal_add(&deco->events.destroy, &d->destroy);
    kde_decorations = g_list_prepend(kde_decorations, d);
}

/* ---- the ObClientImpl ---- */

static void xdg_get_type(ObClient *self)
{
    ObXdgToplevel *x = XDG(self);

    self->transient = x->toplevel->parent != NULL;
    self->modal = toplevel_is_modal(self);

    /* clients drawing their own decorations are like X clients with Motif
       hints turning off the decorations */
    if (!wants_server_decorations(x)) {
        self->mwmhints.flags |= OB_MWM_FLAG_DECORATIONS;
        self->mwmhints.decorations = 0;
    }
}

static void xdg_get_normal_hints(ObClient *self)
{
    struct wlr_xdg_toplevel_state *s = &XDG(self)->toplevel->current;

    if (s->min_width > 0 || s->min_height > 0)
        SIZE_SET(self->min_size, MAX(s->min_width, 1),
                 MAX(s->min_height, 1));
    SIZE_SET(self->max_size,
             s->max_width > 0 ? s->max_width : G_MAXINT,
             s->max_height > 0 ? s->max_height : G_MAXINT);
    /* xdg-shell windows don't ask for positions */
    self->positioned = 0;
    self->sized = 0;
}

static void xdg_get_area(ObClient *self)
{
    struct wlr_box *g = &XDG(self)->toplevel->base->geometry;
    RECT_SET(self->area, 0, 0, MAX(g->width, 1), MAX(g->height, 1));
}

static void xdg_get_state(ObClient *self)
{
    struct wlr_xdg_toplevel_requested *r = &XDG(self)->toplevel->requested;
    if (r->maximized)
        self->max_horz = self->max_vert = TRUE;
    if (r->fullscreen)
        self->fullscreen = TRUE;
    if (r->minimized)
        self->iconic = TRUE;
}

static void xdg_get_class(ObClient *self)
{
    const gchar *app_id = XDG(self)->toplevel->app_id;
    g_free(self->name);
    g_free(self->class);
    self->name = g_strdup(app_id ? app_id : "");
    self->class = g_strdup(app_id ? app_id : "");
}

static gchar* xdg_get_title(ObClient *self)
{
    const gchar *t = XDG(self)->toplevel->title;
    return t ? g_strdup(t) : NULL;
}

static ObClient* xdg_get_parent(ObClient *self)
{
    struct wlr_xdg_toplevel *p = XDG(self)->toplevel->parent;
    ObNode *node;

    if (!p || !p->base->data) return NULL;
    node = ((struct wlr_scene_tree*)p->base->data)->node.data;
    if (node && node->owner && WINDOW_IS_CLIENT(node->owner) &&
        WINDOW_AS_CLIENT(node->owner)->managed)
        return WINDOW_AS_CLIENT(node->owner);
    return NULL;
}

static RrImage* xdg_get_icon(ObClient *self)
{
    return toplevel_find_icon(self);
}

static gboolean xdg_configure(ObClient *self, gint x, gint y, gint w, gint h)
{
    ObXdgToplevel *xt = XDG(self);
    struct wlr_box *g = &xt->toplevel->base->geometry;

    if (!xt->toplevel->base->initialized)
        return FALSE;
    if (w == g->width && h == g->height)
        return FALSE;

    xt->size_serial = wlr_xdg_toplevel_set_size(xt->toplevel, w, h);
    xt->size_requested = TRUE;
    return TRUE;
}

static gboolean xdg_configure_done(ObClient *self, gint w, gint h)
{
    ObXdgToplevel *xt = XDG(self);
    guint32 acked = xt->toplevel->base->current.configure_serial;

    if (!xt->size_requested) return TRUE;
    /* serials wrap around, compare like timestamps */
    if ((gint32)(acked - xt->size_serial) >= 0) {
        xt->size_requested = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void xdg_set_activated(ObClient *self, gboolean activated)
{
    if (XDG(self)->toplevel->base->initialized)
        wlr_xdg_toplevel_set_activated(XDG(self)->toplevel, activated);
}

static void xdg_change_state(ObClient *self)
{
    struct wlr_xdg_toplevel *t = XDG(self)->toplevel;
    guint32 tiled = 0;

    if (!t->base->initialized) return;

    wlr_xdg_toplevel_set_maximized(t, self->max_horz && self->max_vert);
    wlr_xdg_toplevel_set_fullscreen(t, self->fullscreen);
    /* clients can draw resizes differently (or more cheaply) */
    wlr_xdg_toplevel_set_resizing(t, moveresize_resizing(self));
    if (self->max_horz) tiled |= WLR_EDGE_LEFT | WLR_EDGE_RIGHT;
    if (self->max_vert) tiled |= WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
    tiled |= self->snap_edges;
    wlr_xdg_toplevel_set_tiled(t, tiled);
    if (wl_resource_get_version(t->resource) >=
        XDG_TOPLEVEL_STATE_CONSTRAINED_LEFT_SINCE_VERSION)
        wlr_xdg_toplevel_set_constrained(t, tiled);
}

static void xdg_close(ObClient *self)
{
    wlr_xdg_toplevel_send_close(XDG(self)->toplevel);
}

static struct wlr_surface* xdg_surface(ObClient *self)
{
    return XDG(self)->toplevel->base->surface;
}

static void xdg_set_visible(ObClient *self, gboolean visible)
{
    struct wlr_xdg_toplevel *t = XDG(self)->toplevel;
    if (t->base->initialized &&
        wl_resource_get_version(t->resource) >=
        XDG_TOPLEVEL_STATE_SUSPENDED_SINCE_VERSION)
        wlr_xdg_toplevel_set_suspended(t, !visible);
}

static gboolean xdg_ping(ObClient *self)
{
    /* this does nothing while a ping is unanswered */
    wlr_xdg_surface_ping(XDG(self)->toplevel->base);
    return TRUE;
}

static gboolean xdg_ping_pending(ObClient *self)
{
    return XDG(self)->toplevel->base->client->ping_serial != 0;
}

static pid_t xdg_get_pid(ObClient *self)
{
    pid_t pid = 0;
    struct wl_client *wc =
        wl_resource_get_client(XDG(self)->toplevel->base->resource);
    wl_client_get_credentials(wc, &pid, NULL, NULL);
    return pid;
}

const ObClientImpl xdg_client_impl = {
    .get_type = xdg_get_type,
    .get_normal_hints = xdg_get_normal_hints,
    .get_area = xdg_get_area,
    .get_state = xdg_get_state,
    .get_class = xdg_get_class,
    .get_title = xdg_get_title,
    .get_parent = xdg_get_parent,
    .get_desktop = NULL,
    .get_strut = NULL,
    .get_icon = xdg_get_icon,
    .configure = xdg_configure,
    .configure_done = xdg_configure_done,
    .set_activated = xdg_set_activated,
    .change_state = xdg_change_state,
    .close = xdg_close,
    .surface = xdg_surface,
    .set_visible = xdg_set_visible,
    .restack = NULL,
    .get_pid = xdg_get_pid,
    .ping = xdg_ping,
    .ping_pending = xdg_ping_pending,
};

/* ---- toplevel events ---- */

/*! The size a window should start with, for the bounds hint */
static void usable_bounds(gint *w, gint *h)
{
    Rect *a = screen_area(screen_desktop, screen_monitor_pointer(), NULL);
    *w = a->width;
    *h = a->height;
    g_slice_free(Rect, a);
}

static void handle_commit(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, commit);
    struct wlr_xdg_surface *base = x->toplevel->base;
    ObClient *c = x->client;

    if (base->initial_commit) {
        gint bw, bh;

        /* the client gets to choose its size, but let it know how much
           space there is */
        usable_bounds(&bw, &bh);
        if (wl_resource_get_version(x->toplevel->resource) >=
            XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
            wlr_xdg_toplevel_set_bounds(x->toplevel, bw, bh);
        if (wl_resource_get_version(x->toplevel->resource) >=
            XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
            wlr_xdg_toplevel_set_wm_capabilities(x->toplevel,
                WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU |
                WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
                WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN |
                WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);
        decoration_set_mode(x);
        wlr_xdg_toplevel_set_size(x->toplevel, 0, 0);

        /* GTK 3 has dialogs without a parent (X's transient for the
           group), and gives them its focused window as the parent when
           that window is told it is focused.  Tell it again, so the
           dialog has its parent before it is placed. */
        if (focus_client && focus_client->impl == &xdg_client_impl &&
            XDG(focus_client)->toplevel->base->client == base->client &&
            XDG(focus_client)->toplevel->base->initialized)
            wlr_xdg_toplevel_set_activated(XDG(focus_client)->toplevel,
                                           TRUE);
        return;
    }

    if (!base->surface->mapped)
        return;

    if (c->managed) {
        struct wlr_xdg_toplevel_state *s = &x->toplevel->current;
        if (s->min_width != x->min_w || s->min_height != x->min_h ||
            s->max_width != x->max_w || s->max_height != x->max_h)
        {
            x->min_w = s->min_width;
            x->min_h = s->min_height;
            x->max_w = s->max_width;
            x->max_h = s->max_height;
            client_update_normal_hints(c);
            client_setup_decor_and_functions(c, FALSE);
        }
    }

    client_committed(c, base->geometry.width, base->geometry.height);
}

static void handle_map(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, map);
    struct wlr_xdg_toplevel_state *s = &x->toplevel->current;

    x->min_w = s->min_width;
    x->min_h = s->min_height;
    x->max_w = s->max_width;
    x->max_h = s->max_height;

    x->client->committed_size.width = x->toplevel->base->geometry.width;
    x->client->committed_size.height = x->toplevel->base->geometry.height;
    if (ob_state() == OB_STATE_RUNNING)
        client_manage(x->client);
}

static void handle_unmap(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, unmap);
    if (x->client->managed)
        client_unmanage(x->client);
}

static void handle_destroy(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, destroy);

    if (x->client->managed)
        client_unmanage(x->client);
    if (x->decoration) {
        wl_list_remove(&x->decoration_request_mode.link);
        wl_list_remove(&x->decoration_destroy.link);
    }
    wl_list_remove(&x->map.link);
    wl_list_remove(&x->unmap.link);
    wl_list_remove(&x->commit.link);
    wl_list_remove(&x->destroy.link);
    wl_list_remove(&x->request_move.link);
    wl_list_remove(&x->request_resize.link);
    wl_list_remove(&x->request_maximize.link);
    wl_list_remove(&x->request_fullscreen.link);
    wl_list_remove(&x->request_minimize.link);
    wl_list_remove(&x->request_show_window_menu.link);
    wl_list_remove(&x->set_title.link);
    wl_list_remove(&x->set_app_id.link);
    wl_list_remove(&x->set_parent.link);
    client_free(x->client);
    g_free(x);
}

static void handle_request_move(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_move);
    client_request_moveresize(x->client, OB_MOVERESIZE_MOVE);
}

static void handle_request_resize(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    client_request_moveresize(x->client, moveresize_corner_from_edges(ev->edges));
}

static void handle_request_maximize(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_maximize);
    if (!x->toplevel->base->initialized) return;
    if (x->client->managed)
        client_request_maximize(x->client, x->toplevel->requested.maximized);
    else
        wlr_xdg_surface_schedule_configure(x->toplevel->base);
}

static void handle_request_fullscreen(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_fullscreen);
    if (!x->toplevel->base->initialized) return;
    if (x->client->managed)
        client_request_fullscreen(x->client,
                                  x->toplevel->requested.fullscreen);
    else
        wlr_xdg_surface_schedule_configure(x->toplevel->base);
}

static void handle_request_minimize(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_minimize);
    if (x->client->managed)
        client_request_minimize(x->client, x->toplevel->requested.minimized);
}

static void handle_request_show_window_menu(struct wl_listener *l,
                                            void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, request_show_window_menu);
    struct wlr_xdg_toplevel_show_window_menu_event *ev = data;
    gint px, py;

    ob_debug("Window menu requested by %s at %d,%d (button held %d)",
             x->client->title, ev->x, ev->y, seat_pointer_button_held());

    if (!x->client->managed) return;
    /* client side decorations ask for the window menu, show Openbox's */
    px = x->client->frame->area.x + x->client->frame->size.left -
        x->toplevel->base->geometry.x + ev->x;
    py = x->client->frame->area.y + x->client->frame->size.top -
        x->toplevel->base->geometry.y + ev->y;
    menu_show_client_menu(x->client, px, py);
}

static void handle_set_title(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, set_title);
    if (x->client->managed)
        client_update_title(x->client);
}

static void handle_set_app_id(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, set_app_id);
    if (x->client->managed) {
        xdg_get_class(x->client);
        client_update_icons(x->client);
        toplevel_update_app_id(x->client);
    }
}

static void handle_set_parent(struct wl_listener *l, void *data)
{
    ObXdgToplevel *x = wl_container_of(l, x, set_parent);
    if (x->client->managed) {
        client_update_transient_for(x->client);
        client_calc_layer(x->client);
    }
}

static void handle_new_toplevel(struct wl_listener *l, void *data)
{
    struct wlr_xdg_toplevel *toplevel = data;
    ObXdgToplevel *x = g_new0(ObXdgToplevel, 1);

    x->toplevel = toplevel;
    x->tree = wlr_scene_xdg_surface_create(server.unmanaged_tree,
                                           toplevel->base);
    wlr_scene_node_set_enabled(&x->tree->node, FALSE);
    toplevel->base->data = x->tree;
    x->client = client_new(&xdg_client_impl, x, x->tree);

#define LISTEN(sig, l, f) do { x->l.notify = (f); wl_signal_add((sig), &x->l); } while (0)
    LISTEN(&toplevel->base->surface->events.map, map, handle_map);
    LISTEN(&toplevel->base->surface->events.unmap, unmap, handle_unmap);
    LISTEN(&toplevel->base->surface->events.commit, commit, handle_commit);
    LISTEN(&toplevel->events.destroy, destroy, handle_destroy);
    LISTEN(&toplevel->events.request_move, request_move, handle_request_move);
    LISTEN(&toplevel->events.request_resize, request_resize,
           handle_request_resize);
    LISTEN(&toplevel->events.request_maximize, request_maximize,
           handle_request_maximize);
    LISTEN(&toplevel->events.request_fullscreen, request_fullscreen,
           handle_request_fullscreen);
    LISTEN(&toplevel->events.request_minimize, request_minimize,
           handle_request_minimize);
    LISTEN(&toplevel->events.request_show_window_menu,
           request_show_window_menu, handle_request_show_window_menu);
    LISTEN(&toplevel->events.set_title, set_title, handle_set_title);
    LISTEN(&toplevel->events.set_app_id, set_app_id, handle_set_app_id);
    LISTEN(&toplevel->events.set_parent, set_parent, handle_set_parent);
#undef LISTEN
}

/* ---- popups ---- */

/* Keep popups on the output that their window is on */
static void popup_unconstrain(struct wlr_xdg_popup *popup)
{
    struct wlr_xdg_surface *root = popup->base;
    struct wlr_scene_tree *root_tree;
    gint rx, ry, tx, ty;
    struct wlr_output *o;
    struct wlr_box obox, box;

    /* find the root (toplevel or layer surface) */
    while (root->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        struct wlr_xdg_surface *p =
            wlr_xdg_surface_try_from_wlr_surface(root->popup->parent);
        if (!p) break;
        root = p;
    }
    root_tree = root->data;
    if (!root_tree) {
        /* a layer surface's popup, see layers.c */
        layers_popup_unconstrain(popup);
        return;
    }
    /* the root surface's position in the layout */
    wlr_scene_node_coords(&root_tree->node, &rx, &ry);
    /* the popup's position relative to the root surface */
    wlr_xdg_popup_get_toplevel_coords(popup, popup->scheduled.geometry.x,
                                      popup->scheduled.geometry.y, &tx, &ty);
    o = wlr_output_layout_output_at(server.output_layout, rx + tx, ry + ty);
    if (!o)
        o = wlr_output_layout_get_center_output(server.output_layout);
    if (!o) return;
    wlr_output_layout_get_box(server.output_layout, o, &obox);

    box.x = obox.x - rx;
    box.y = obox.y - ry;
    box.width = obox.width;
    box.height = obox.height;
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

static void popup_handle_commit(struct wl_listener *l, void *data)
{
    ObXdgPopup *p = wl_container_of(l, p, commit);
    if (p->popup->base->initial_commit)
        popup_unconstrain(p->popup);
}

static void popup_handle_destroy(struct wl_listener *l, void *data)
{
    ObXdgPopup *p = wl_container_of(l, p, destroy);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    g_free(p);
}

void xdg_popup_add(struct wlr_xdg_popup *popup,
                   struct wlr_scene_tree *parent_tree)
{
    ObXdgPopup *p;

    popup->base->data = wlr_scene_xdg_surface_create(parent_tree,
                                                     popup->base);

    p = g_new0(ObXdgPopup, 1);
    p->popup = popup;
    p->commit.notify = popup_handle_commit;
    wl_signal_add(&popup->base->surface->events.commit, &p->commit);
    p->destroy.notify = popup_handle_destroy;
    wl_signal_add(&popup->events.destroy, &p->destroy);
}

static void handle_new_popup(struct wl_listener *l, void *data)
{
    struct wlr_xdg_popup *popup = data;
    struct wlr_xdg_surface *parent;
    struct wlr_scene_tree *parent_tree;

    /* a layer surface's popup is made without a parent, which it gets
       from the layer surface (get_popup) before its first commit, see
       layers.c */
    if (!popup->parent) return;
    parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (parent)
        parent_tree = parent->data;
    else
        /* a layer shell surface's popup */
        parent_tree = layers_popup_parent_tree(popup->parent);
    if (!parent_tree) return;
    xdg_popup_add(popup, parent_tree);
}

void xdg_init(void)
{
    server.xdg_shell = wlr_xdg_shell_create(server.display, 7);
    /* ping.c decides when a client isn't responding, keep wlroots from
       giving up on the ping before that */
    server.xdg_shell->ping_timeout = 10 * 60 * 1000;
    new_toplevel.notify = handle_new_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &new_toplevel);
    new_popup.notify = handle_new_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &new_popup);

    server.xdg_decoration =
        wlr_xdg_decoration_manager_v1_create(server.display);
    new_decoration.notify = handle_new_decoration;
    wl_signal_add(&server.xdg_decoration->events.new_toplevel_decoration,
                  &new_decoration);

    server.kde_decoration =
        wlr_server_decoration_manager_create(server.display);
    wlr_server_decoration_manager_set_default_mode(server.kde_decoration,
        config_decoration_policy == OB_DECORATION_POLICY_CLIENT ?
        WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT :
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    new_kde_decoration.notify = handle_new_kde_decoration;
    wl_signal_add(&server.kde_decoration->events.new_decoration,
                  &new_kde_decoration);
}

void xdg_finish(void)
{
    wl_list_remove(&new_toplevel.link);
    wl_list_remove(&new_popup.link);
    wl_list_remove(&new_decoration.link);
    wl_list_remove(&new_kde_decoration.link);
}
