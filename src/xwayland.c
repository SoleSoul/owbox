/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xwayland.c for owbox
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

/* X11 applications through Xwayland.  wlroots' window manager (the xwm)
   reads the X properties and sends the X requests, and this is where they
   become Openbox's clients again: the property reading and the client
   message handling of Openbox's client.c and event.c.

   Override-redirect windows (menus, tooltips) are not managed, they are
   shown where they put themselves, above the managed windows. */

#include "xwayland.h"
#include "client.h"
#include "frame.h"
#include "server.h"
#include "config.h"
#include "screen.h"
#include "stacking.h"
#include "openbox.h"
#include "event.h"
#include "moveresize.h"
#include "seat.h"
#include "toplevel.h"
#include "window.h"
#include "gravity.h"
#include "debug.h"

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/server.h>
#include <wlr/xwayland/shell.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>

typedef struct {
    /*! An override-redirect window is an ObWindow of its own, a managed one
      is an ObClient */
    ObWindow obwin;
    ObNode node;

    struct wlr_xwayland_surface *xs;
    /*! Holds the window's surfaces, it is the ObClient's surface tree */
    struct wlr_scene_tree *tree;
    /*! The surface's scene, while the X window has a surface */
    struct wlr_scene_tree *surface_tree;
    ObClient *client;
    /*! Shown as an override-redirect window */
    gboolean unmanaged;

    struct wl_listener destroy;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener surface_tree_destroy;
    struct wl_listener request_configure;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_activate;
    struct wl_listener request_close;
    struct wl_listener request_shaded;
    struct wl_listener request_skip_taskbar;
    struct wl_listener request_skip_pager;
    struct wl_listener request_above;
    struct wl_listener request_below;
    struct wl_listener request_demands_attention;
    struct wl_listener set_title;
    struct wl_listener set_class;
    struct wl_listener set_role;
    struct wl_listener set_parent;
    struct wl_listener set_window_type;
    struct wl_listener set_hints;
    struct wl_listener set_size_hints;
    struct wl_listener set_decorations;
    struct wl_listener set_strut_partial;
    struct wl_listener set_override_redirect;
    struct wl_listener set_geometry;
    struct wl_listener set_icon;
} ObXwSurface;

static struct wl_listener new_surface;
static struct wl_listener ready;

#define XW(c) ((ObXwSurface*)(c)->impl_data)

static ObClient* client_of(struct wlr_xwayland_surface *xs)
{
    ObXwSurface *x = xs ? xs->data : NULL;
    return x && x->client && x->client->managed ? x->client : NULL;
}

/* ---- the ObClientImpl ---- */

static void xw_get_type(ObClient *self)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    static const struct {
        enum wlr_xwayland_net_wm_window_type x;
        ObClientType ob;
    } types[] = {
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP, OB_CLIENT_TYPE_DESKTOP },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK, OB_CLIENT_TYPE_DOCK },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR, OB_CLIENT_TYPE_TOOLBAR },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU, OB_CLIENT_TYPE_MENU },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY, OB_CLIENT_TYPE_UTILITY },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH, OB_CLIENT_TYPE_SPLASH },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG, OB_CLIENT_TYPE_DIALOG },
        { WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL, OB_CLIENT_TYPE_NORMAL },
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(types); ++i)
        if (wlr_xwayland_surface_has_window_type(xs, types[i].x)) {
            self->type = types[i].ob;
            break;
        }

    self->transient = xs->parent != NULL;
    self->modal = xs->modal;

    /* the Motif hints, which wlroots boils down to the title and border */
    if (xs->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL) {
        self->mwmhints.flags |= OB_MWM_FLAG_DECORATIONS;
        self->mwmhints.decorations = 0;
        if (!(xs->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE))
            self->mwmhints.decorations |= OB_MWM_DECOR_TITLE |
                OB_MWM_DECOR_ICONIFY | OB_MWM_DECOR_MAXIMIZE;
        if (!(xs->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER))
            self->mwmhints.decorations |= OB_MWM_DECOR_BORDER |
                OB_MWM_DECOR_HANDLE;
    }
}

static void xw_get_normal_hints(ObClient *self)
{
    xcb_size_hints_t *size = XW(self)->xs->size_hints;

    self->positioned = 0;
    self->sized = 0;
    if (!size) return;

    /* this is client_update_normal_hints() from Openbox */
    self->positioned = (size->flags & (XCB_ICCCM_SIZE_HINT_P_POSITION |
                                       XCB_ICCCM_SIZE_HINT_US_POSITION));
    self->sized = (size->flags & (XCB_ICCCM_SIZE_HINT_P_SIZE |
                                  XCB_ICCCM_SIZE_HINT_US_SIZE));

    if (size->flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY)
        self->gravity = size->win_gravity;

    if (size->flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
        if (size->min_aspect_den)
            self->min_ratio = (gfloat) size->min_aspect_num /
                size->min_aspect_den;
        if (size->max_aspect_den)
            self->max_ratio = (gfloat) size->max_aspect_num /
                size->max_aspect_den;
    }

    if (size->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
        SIZE_SET(self->min_size, size->min_width, size->min_height);

    if (size->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
        SIZE_SET(self->max_size, size->max_width, size->max_height);

    if (size->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
        SIZE_SET(self->base_size, size->base_width, size->base_height);

    if (size->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC &&
        size->width_inc && size->height_inc)
        SIZE_SET(self->size_inc, size->width_inc, size->height_inc);
}

static void xw_get_area(ObClient *self)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    RECT_SET(self->area, xs->x, xs->y, MAX(xs->width, 1), MAX(xs->height, 1));
}

static void xw_get_state(ObClient *self)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;

    /* the _NET_WM_STATE the window asks for when it maps */
    self->modal = xs->modal;
    self->shaded = xs->shaded;
    self->iconic = xs->minimized;
    self->skip_taskbar = xs->skip_taskbar;
    self->skip_pager = xs->skip_pager;
    self->fullscreen = xs->fullscreen;
    self->max_vert = xs->maximized_vert;
    self->max_horz = xs->maximized_horz;
    self->above = xs->above;
    self->below = xs->below;
    self->demands_attention = xs->demands_attention;
}

static void xw_get_class(ObClient *self)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    g_free(self->name);
    g_free(self->class);
    g_free(self->role);
    self->name = g_strdup(xs->instance ? xs->instance : "");
    self->class = g_strdup(xs->class ? xs->class : "");
    self->role = g_strdup(xs->role ? xs->role : "");
}

static gchar* xw_get_title(ObClient *self)
{
    const gchar *t = XW(self)->xs->title;
    return t ? g_strdup(t) : NULL;
}

static ObClient* xw_get_parent(ObClient *self)
{
    return client_of(XW(self)->xs->parent);
}

static gboolean xw_get_desktop(ObClient *self, guint *desktop)
{
    /* wlroots doesn't read _NET_WM_DESKTOP, but sticky windows want to be
       on all of them */
    if (XW(self)->xs->sticky) {
        *desktop = DESKTOP_ALL;
        return TRUE;
    }
    return FALSE;
}

static void xw_get_strut(ObClient *self, StrutPartial *strut)
{
    xcb_ewmh_wm_strut_partial_t *s = XW(self)->xs->strut_partial;
    if (!s) return;
    STRUT_PARTIAL_SET(*strut, s->left, s->top, s->right, s->bottom,
                      s->left_start_y, s->left_end_y,
                      s->top_start_x, s->top_end_x,
                      s->right_start_y, s->right_end_y,
                      s->bottom_start_x, s->bottom_end_x);
}

static RrImage* xw_get_icon(ObClient *self)
{
    xcb_ewmh_get_wm_icon_reply_t reply;
    xcb_ewmh_wm_icon_iterator_t it;
    RrImage *img = NULL;

    /* _NET_WM_ICON, its pixels are ARGB like RrPixel32 */
    if (wlr_xwayland_surface_fetch_icon(XW(self)->xs, &reply)) {
        for (it = xcb_ewmh_get_wm_icon_iterator(&reply); it.rem;
             xcb_ewmh_get_wm_icon_next(&it))
        {
            if (!it.width || !it.height || it.width > 1024 ||
                it.height > 1024)
                continue;
            if (!img)
                img = RrImageNewFromData(ob_rr_icons, it.data,
                                         it.width, it.height);
            else
                RrImageAddFromData(img, it.data, it.width, it.height);
        }
        xcb_ewmh_get_wm_icon_reply_wipe(&reply);
    }
    /* or the icon of the application's desktop entry */
    return img ? img : toplevel_find_icon(self);
}

static void xw_get_wmhints(ObClient *self, gboolean *urgent,
                           gboolean *iconic)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    xcb_icccm_wm_hints_t *hints = xs->hints;
    enum wlr_xwayland_icccm_input_model model;

    if (hints) {
        if (hints->flags & XCB_ICCCM_WM_HINT_INPUT)
            self->can_focus = hints->input;
        if (hints->flags & XCB_ICCCM_WM_HINT_STATE)
            *iconic = hints->initial_state == XCB_ICCCM_WM_STATE_ICONIC;
        *urgent = (hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY) != 0;
    }
    /* WM_TAKE_FOCUS, wlroots offers the focus to these when activating */
    model = wlr_xwayland_surface_icccm_input_model(xs);
    self->focus_notify = (model == WLR_ICCCM_INPUT_MODEL_LOCAL ||
                          model == WLR_ICCCM_INPUT_MODEL_GLOBAL);
}

static gboolean xw_configure(ObClient *self, gint x, gint y, gint w, gint h)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    struct wlr_surface *s = xs->surface;
    gboolean resize;

    /* the X window moves too, X clients use their root position for
       placing popups.  This always sends a ConfigureNotify, which is what
       Openbox's synthetic ones were for */
    wlr_xwayland_surface_configure(xs, x, y, w, h);

    /* the client draws at the new size, and commits that */
    resize = s && s->mapped &&
        (w != s->current.width || h != s->current.height);
    return resize;
}

static gboolean xw_configure_done(ObClient *self, gint w, gint h)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;
    /* an X window is exactly the size it was given */
    return w == xs->width && h == xs->height;
}

static void xw_set_activated(ObClient *self, gboolean activated)
{
    wlr_xwayland_surface_activate(XW(self)->xs, activated);
}

static void xw_change_state(ObClient *self)
{
    struct wlr_xwayland_surface *xs = XW(self)->xs;

    /* this is the _NET_WM_STATE part of Openbox's client_change_state() */
    wlr_xwayland_surface_set_maximized(xs, self->max_horz, self->max_vert);
    wlr_xwayland_surface_set_fullscreen(xs, self->fullscreen);
    wlr_xwayland_surface_set_shaded(xs, self->shaded);
    wlr_xwayland_surface_set_sticky(xs, self->desktop == DESKTOP_ALL);
    wlr_xwayland_surface_set_skip_taskbar(xs, self->skip_taskbar);
    wlr_xwayland_surface_set_skip_pager(xs, self->skip_pager);
    wlr_xwayland_surface_set_above(xs, self->above);
    wlr_xwayland_surface_set_below(xs, self->below);
    wlr_xwayland_surface_set_demands_attention(xs, self->demands_attention);
}

static void xw_close(ObClient *self)
{
    wlr_xwayland_surface_close(XW(self)->xs);
}

static struct wlr_surface* xw_surface(ObClient *self)
{
    return XW(self)->xs->surface;
}

static void xw_set_visible(ObClient *self, gboolean visible)
{
    /* According to the ICCCM (sec 4.1.3.1) when a window is not visible,
       it needs to be in IconicState, this includes being on another
       desktop */
    wlr_xwayland_surface_set_minimized(XW(self)->xs, !visible);
}

static void xw_restack(ObClient *self)
{
    /* stacking.c restacks from the bottom up, keep the X stacking order
       the same for X clients that look at it */
    wlr_xwayland_surface_restack(XW(self)->xs, NULL, XCB_STACK_MODE_ABOVE);
}

static pid_t xw_get_pid(ObClient *self)
{
    return XW(self)->xs->pid;
}

static const ObClientImpl xwayland_client_impl = {
    .get_type = xw_get_type,
    .get_normal_hints = xw_get_normal_hints,
    .get_area = xw_get_area,
    .get_state = xw_get_state,
    .get_class = xw_get_class,
    .get_title = xw_get_title,
    .get_parent = xw_get_parent,
    .get_desktop = xw_get_desktop,
    .get_strut = xw_get_strut,
    .get_icon = xw_get_icon,
    .get_wmhints = xw_get_wmhints,
    .configure = xw_configure,
    .configure_done = xw_configure_done,
    .set_activated = xw_set_activated,
    .change_state = xw_change_state,
    .close = xw_close,
    .surface = xw_surface,
    .set_visible = xw_set_visible,
    .restack = xw_restack,
    .get_pid = xw_get_pid,
};

/* ---- override-redirect windows ---- */

static void unmanaged_show(ObXwSurface *x)
{
    struct wlr_xwayland_surface *xs = x->xs;

    x->unmanaged = TRUE;
    ob_node_attach(&x->node, &x->tree->node);
    wlr_scene_node_reparent(&x->tree->node, server.xwayland_tree);
    wlr_scene_node_set_position(&x->tree->node, xs->x, xs->y);
    wlr_scene_node_raise_to_top(&x->tree->node);
    wlr_scene_node_set_enabled(&x->tree->node, TRUE);

    /* things like dmenu and rofi on X grab the keyboard */
    if (wlr_xwayland_surface_override_redirect_wants_focus(xs))
        seat_focus_surface(xs->surface);
}

static void unmanaged_hide(ObXwSurface *x)
{
    x->unmanaged = FALSE;
    x->tree->node.data = NULL;
    wlr_scene_node_reparent(&x->tree->node, server.unmanaged_tree);
    if (x->xs->surface)
        seat_surface_gone(x->xs->surface);
}

/* ---- events ---- */

static void map_window(ObXwSurface *x)
{
    if (x->xs->override_redirect) {
        unmanaged_show(x);
        return;
    }

    x->client = client_new(&xwayland_client_impl, x, x->tree);
    x->client->committed_size.width = x->xs->surface->current.width;
    x->client->committed_size.height = x->xs->surface->current.height;
    if (ob_state() == OB_STATE_RUNNING)
        client_manage(x->client);
}

static void unmap_window(ObXwSurface *x)
{
    if (x->unmanaged)
        unmanaged_hide(x);
    if (x->client) {
        if (x->client->managed)
            client_unmanage(x->client);
        client_free(x->client);
        x->client = NULL;
    }
}

static void handle_map(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, map);
    map_window(x);
}

static void handle_unmap(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, unmap);
    unmap_window(x);
}

static void handle_commit(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, commit);
    struct wlr_surface *s = x->xs->surface;

    if (x->client)
        client_committed(x->client, s->current.width, s->current.height);
}

static void handle_surface_tree_destroy(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, surface_tree_destroy);
    wl_list_remove(&x->surface_tree_destroy.link);
    x->surface_tree = NULL;
}

static void handle_associate(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, associate);
    struct wlr_surface *s = x->xs->surface;

    x->surface_tree = wlr_scene_subsurface_tree_create(x->tree, s);
    x->surface_tree_destroy.notify = handle_surface_tree_destroy;
    wl_signal_add(&x->surface_tree->node.events.destroy,
                  &x->surface_tree_destroy);

    x->map.notify = handle_map;
    wl_signal_add(&s->events.map, &x->map);
    x->unmap.notify = handle_unmap;
    wl_signal_add(&s->events.unmap, &x->unmap);
    x->commit.notify = handle_commit;
    wl_signal_add(&s->events.commit, &x->commit);
}

static void handle_dissociate(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, dissociate);

    unmap_window(x);
    wl_list_remove(&x->map.link);
    wl_list_remove(&x->unmap.link);
    wl_list_remove(&x->commit.link);
    if (x->surface_tree)
        wlr_scene_node_destroy(&x->surface_tree->node);
}

/* Openbox's handling of a ConfigureRequest */
static void configure_request(ObClient *client,
                              struct wlr_xwayland_surface_configure_event *e)
{
    gint x, y, w, h;
    gboolean move = FALSE;
    gboolean resize = FALSE;

    /* get the current area */
    RECT_TO_DIMS(client->area, x, y, w, h);

    ob_debug("ConfigureRequest for \"%s\" desktop %d wmstate %d "
             "visible %d",
             client->title,
             screen_desktop, client->wmstate, client->frame->visible);
    ob_debug("                     x %d y %d w %d h %d b %d",
             x, y, w, h, client->border_width);

    if (e->mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        gulong ignore_start = 0;

        /* wlroots doesn't pass on the sibling or the stack mode, so this
           is a request to be raised, the most common one */
        if (!config_focus_under_mouse)
            ignore_start = event_start_ignore_all_enters();
        stacking_restack_request(client, NULL, XCB_STACK_MODE_ABOVE);
        if (!config_focus_under_mouse)
            event_end_ignore_all_enters(ignore_start);

        /* a stacking change moves the window without resizing */
        move = TRUE;
    }

    if (e->mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                   XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT))
    {
        /* don't allow clients to move shaded windows (fvwm does this)
         */
        if (e->mask & XCB_CONFIG_WINDOW_X) {
            if (!client->shaded)
                x = e->x;
            move = TRUE;
        }
        if (e->mask & XCB_CONFIG_WINDOW_Y) {
            if (!client->shaded)
                y = e->y;
            move = TRUE;
        }

        if (e->mask & XCB_CONFIG_WINDOW_WIDTH) {
            w = e->width;
            resize = TRUE;
        }
        if (e->mask & XCB_CONFIG_WINDOW_HEIGHT) {
            h = e->height;
            resize = TRUE;
        }
    }

    ob_debug("ConfigureRequest x(%d) %d y(%d) %d w(%d) %d h(%d) %d "
             "move %d resize %d",
             e->mask & XCB_CONFIG_WINDOW_X, x,
             e->mask & XCB_CONFIG_WINDOW_Y, y,
             e->mask & XCB_CONFIG_WINDOW_WIDTH, w,
             e->mask & XCB_CONFIG_WINDOW_HEIGHT, h,
             move, resize);

    /* check for broken apps moving to their root position

       XXX remove this some day...that would be nice. right now all
       kde apps do this when they try activate themselves on another
       desktop. eg. open amarok window on desktop 1, switch to desktop
       2, click amarok tray icon. it will move by its decoration size.
    */
    if (x != client->area.x &&
        x == (client->frame->area.x + client->frame->size.left -
              (gint)client->border_width) &&
        y != client->area.y &&
        y == (client->frame->area.y + client->frame->size.top -
              (gint)client->border_width) &&
        w == client->area.width &&
        h == client->area.height)
    {
        ob_debug_type(OB_DEBUG_APP_BUGS,
                      "Application %s is trying to move via "
                      "ConfigureRequest to it's root window position "
                      "but it is not using StaticGravity",
                      client->title);
        /* don't move it */
        x = client->area.x;
        y = client->area.y;

        /* they still requested a move, so don't change whether a
           notify is sent or not */
    }

    {
        gint lw, lh;

        client_try_configure(client, &x, &y, &w, &h, &lw, &lh, FALSE);

        /* if x was not given, then use gravity to figure out the new
           x.  the reference point should not be moved */
        if ((e->mask & XCB_CONFIG_WINDOW_WIDTH &&
             !(e->mask & XCB_CONFIG_WINDOW_X)))
            client_gravity_resize_w(client, &x, client->area.width, w);
        /* same for y */
        if ((e->mask & XCB_CONFIG_WINDOW_HEIGHT &&
             !(e->mask & XCB_CONFIG_WINDOW_Y)))
            client_gravity_resize_h(client, &y, client->area.height, h);

        client_find_onscreen(client, &x, &y, w, h, FALSE);

        ob_debug("Granting ConfigureRequest x %d y %d w %d h %d",
                 x, y, w, h);
        client_configure(client, x, y, w, h, FALSE, TRUE, TRUE);
    }
}

static void handle_request_configure(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_configure);
    struct wlr_xwayland_surface_configure_event *e = data;
    ObClient *c = client_of(x->xs);

    if (c)
        configure_request(c, e);
    else
        /* not managed (yet), let it do what it wants like X does */
        wlr_xwayland_surface_configure(x->xs, e->x, e->y,
                                       e->width, e->height);
}

static void handle_request_move(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_move);
    ObClient *c = client_of(x->xs);
    if (c) client_request_moveresize(c, OB_MOVERESIZE_MOVE);
}

static void handle_request_resize(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_resize);
    struct wlr_xwayland_resize_event *ev = data;
    ObClient *c = client_of(x->xs);
    if (c)
        client_request_moveresize(c, moveresize_corner_from_edges(ev->edges));
}

static void handle_request_minimize(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_minimize);
    struct wlr_xwayland_minimize_event *ev = data;
    ObClient *c = client_of(x->xs);
    if (c) client_request_minimize(c, ev->minimize);
}

static void handle_request_maximize(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_maximize);
    ObClient *c = client_of(x->xs);

    if (!c) return;
    /* _NET_WM_STATE_MAXIMIZED_HORZ and _VERT separately, like Openbox */
    if (x->xs->maximized_horz != c->max_horz)
        client_maximize(c, x->xs->maximized_horz, 1);
    if (x->xs->maximized_vert != c->max_vert)
        client_maximize(c, x->xs->maximized_vert, 2);
    /* the state may not have changed, tell it what it is */
    client_change_state(c);
}

static void handle_request_fullscreen(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_fullscreen);
    ObClient *c = client_of(x->xs);
    if (c) client_request_fullscreen(c, x->xs->fullscreen);
}

static void handle_request_activate(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_activate);
    ObClient *c = client_of(x->xs);
    /* _NET_ACTIVE_WINDOW, the source isn't known, treat it as from the
       application, which focus stealing prevention applies to */
    if (c) client_activate(c, FALSE, FALSE, TRUE, TRUE, FALSE);
}

static void handle_request_close(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_close);
    ObClient *c = client_of(x->xs);
    if (c) client_close(c);
}

static void handle_request_shaded(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_shaded);
    ObClient *c = client_of(x->xs);
    if (c) client_shade(c, x->xs->shaded);
}

static void handle_request_skip_taskbar(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_skip_taskbar);
    ObClient *c = client_of(x->xs);
    if (c) {
        c->skip_taskbar = x->xs->skip_taskbar;
        client_change_state(c);
    }
}

static void handle_request_skip_pager(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_skip_pager);
    ObClient *c = client_of(x->xs);
    if (c) {
        c->skip_pager = x->xs->skip_pager;
        client_change_state(c);
    }
}

static void handle_request_above(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_above);
    ObClient *c = client_of(x->xs);
    if (c) client_set_layer(c, x->xs->above ? 1 : (c->below ? -1 : 0));
}

static void handle_request_below(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_below);
    ObClient *c = client_of(x->xs);
    if (c) client_set_layer(c, x->xs->below ? -1 : (c->above ? 1 : 0));
}

static void handle_request_demands_attention(struct wl_listener *l,
                                             void *data)
{
    ObXwSurface *x = wl_container_of(l, x, request_demands_attention);
    ObClient *c = client_of(x->xs);
    if (c) client_hilite(c, x->xs->demands_attention);
}

static void handle_set_title(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_title);
    ObClient *c = client_of(x->xs);
    if (c) client_update_title(c);
}

static void handle_set_class(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_class);
    ObClient *c = client_of(x->xs);
    if (c) {
        xw_get_class(c);
        client_update_icons(c);
        toplevel_update_app_id(c);
    }
}

static void handle_set_role(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_role);
    ObClient *c = client_of(x->xs);
    if (c) xw_get_class(c);
}

static void handle_set_parent(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_parent);
    ObClient *c = client_of(x->xs);
    if (c) {
        client_update_transient_for(c);
        client_calc_layer(c);
    }
}

static void handle_set_window_type(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_window_type);
    ObClient *c = client_of(x->xs);
    if (c) {
        client_get_type_and_transientness(c);
        client_setup_decor_and_functions(c, TRUE);
        client_calc_layer(c);
    }
}

static void handle_set_hints(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_hints);
    ObClient *c = client_of(x->xs);
    if (c) client_update_wmhints(c);
}

static void handle_set_size_hints(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_size_hints);
    ObClient *c = client_of(x->xs);
    if (c) {
        client_update_normal_hints(c);
        /* normal hints can make a window non-resizable */
        client_setup_decor_and_functions(c, FALSE);
        /* make sure the client's sizes are within its bounds, but only
           reconfigure the window if it needs to. emacs will update its
           normal hints every time it receives a ConfigureNotify */
        client_reconfigure(c, FALSE);
    }
}

static void handle_set_decorations(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_decorations);
    ObClient *c = client_of(x->xs);
    if (c) {
        client_get_type_and_transientness(c);
        client_setup_decor_and_functions(c, TRUE);
    }
}

static void handle_set_strut_partial(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_strut_partial);
    ObClient *c = client_of(x->xs);
    if (c) client_update_strut(c);
}

static void handle_set_icon(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_icon);
    ObClient *c = client_of(x->xs);
    if (c) client_update_icons(c);
}

static void handle_set_geometry(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_geometry);
    /* override-redirect windows move themselves */
    if (x->unmanaged)
        wlr_scene_node_set_position(&x->tree->node, x->xs->x, x->xs->y);
}

static void handle_set_override_redirect(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, set_override_redirect);
    struct wlr_surface *s = x->xs->surface;

    /* it becomes managed or unmanaged */
    if (s && s->mapped) {
        unmap_window(x);
        map_window(x);
    }
}

static void handle_destroy(struct wl_listener *l, void *data)
{
    ObXwSurface *x = wl_container_of(l, x, destroy);

    unmap_window(x);
    wl_list_remove(&x->destroy.link);
    wl_list_remove(&x->associate.link);
    wl_list_remove(&x->dissociate.link);
    wl_list_remove(&x->request_configure.link);
    wl_list_remove(&x->request_move.link);
    wl_list_remove(&x->request_resize.link);
    wl_list_remove(&x->request_minimize.link);
    wl_list_remove(&x->request_maximize.link);
    wl_list_remove(&x->request_fullscreen.link);
    wl_list_remove(&x->request_activate.link);
    wl_list_remove(&x->request_close.link);
    wl_list_remove(&x->request_shaded.link);
    wl_list_remove(&x->request_skip_taskbar.link);
    wl_list_remove(&x->request_skip_pager.link);
    wl_list_remove(&x->request_above.link);
    wl_list_remove(&x->request_below.link);
    wl_list_remove(&x->request_demands_attention.link);
    wl_list_remove(&x->set_title.link);
    wl_list_remove(&x->set_class.link);
    wl_list_remove(&x->set_role.link);
    wl_list_remove(&x->set_parent.link);
    wl_list_remove(&x->set_window_type.link);
    wl_list_remove(&x->set_hints.link);
    wl_list_remove(&x->set_size_hints.link);
    wl_list_remove(&x->set_decorations.link);
    wl_list_remove(&x->set_strut_partial.link);
    wl_list_remove(&x->set_override_redirect.link);
    wl_list_remove(&x->set_geometry.link);
    wl_list_remove(&x->set_icon.link);
    if (x->surface_tree) {
        wl_list_remove(&x->surface_tree_destroy.link);
        x->surface_tree = NULL;
    }
    wlr_scene_node_destroy(&x->tree->node);
    x->xs->data = NULL;
    g_free(x);
}

static void handle_new_surface(struct wl_listener *l, void *data)
{
    struct wlr_xwayland_surface *xs = data;
    ObXwSurface *x = g_new0(ObXwSurface, 1);

    x->obwin.type = OB_WINDOW_CLASS_UNMANAGED;
    x->node.type = OB_NODE_WINDOW;
    x->node.owner = &x->obwin;
    x->xs = xs;
    xs->data = x;
    x->tree = wlr_scene_tree_create(server.unmanaged_tree);

#define LISTEN(sig, l, f) do { x->l.notify = (f); wl_signal_add((sig), &x->l); } while (0)
    LISTEN(&xs->events.destroy, destroy, handle_destroy);
    LISTEN(&xs->events.associate, associate, handle_associate);
    LISTEN(&xs->events.dissociate, dissociate, handle_dissociate);
    LISTEN(&xs->events.request_configure, request_configure,
           handle_request_configure);
    LISTEN(&xs->events.request_move, request_move, handle_request_move);
    LISTEN(&xs->events.request_resize, request_resize, handle_request_resize);
    LISTEN(&xs->events.request_minimize, request_minimize,
           handle_request_minimize);
    LISTEN(&xs->events.request_maximize, request_maximize,
           handle_request_maximize);
    LISTEN(&xs->events.request_fullscreen, request_fullscreen,
           handle_request_fullscreen);
    LISTEN(&xs->events.request_activate, request_activate,
           handle_request_activate);
    LISTEN(&xs->events.request_close, request_close, handle_request_close);
    LISTEN(&xs->events.request_shaded, request_shaded, handle_request_shaded);
    LISTEN(&xs->events.request_skip_taskbar, request_skip_taskbar,
           handle_request_skip_taskbar);
    LISTEN(&xs->events.request_skip_pager, request_skip_pager,
           handle_request_skip_pager);
    LISTEN(&xs->events.request_above, request_above, handle_request_above);
    LISTEN(&xs->events.request_below, request_below, handle_request_below);
    LISTEN(&xs->events.request_demands_attention, request_demands_attention,
           handle_request_demands_attention);
    LISTEN(&xs->events.set_title, set_title, handle_set_title);
    LISTEN(&xs->events.set_class, set_class, handle_set_class);
    LISTEN(&xs->events.set_role, set_role, handle_set_role);
    LISTEN(&xs->events.set_parent, set_parent, handle_set_parent);
    LISTEN(&xs->events.set_window_type, set_window_type,
           handle_set_window_type);
    LISTEN(&xs->events.set_hints, set_hints, handle_set_hints);
    LISTEN(&xs->events.set_size_hints, set_size_hints, handle_set_size_hints);
    LISTEN(&xs->events.set_decorations, set_decorations,
           handle_set_decorations);
    LISTEN(&xs->events.set_strut_partial, set_strut_partial,
           handle_set_strut_partial);
    LISTEN(&xs->events.set_override_redirect, set_override_redirect,
           handle_set_override_redirect);
    LISTEN(&xs->events.set_geometry, set_geometry, handle_set_geometry);
    LISTEN(&xs->events.set_icon, set_icon, handle_set_icon);
#undef LISTEN

    /* a surface that already exists */
    if (xs->surface) {
        handle_associate(&x->associate, NULL);
        if (xs->surface->mapped)
            map_window(x);
    }
}

/* ---- the server ---- */

void xwayland_update_cursor(void)
{
    struct wlr_xcursor *xc;
    struct wlr_xcursor_image *img;

    if (!server.xwayland || !server.xcursor_manager) return;
    /* the cursor for X windows that don't set one, like the root window */
    if (!wlr_xcursor_manager_load(server.xcursor_manager, 1)) return;
    xc = wlr_xcursor_manager_get_xcursor(server.xcursor_manager,
                                         "default", 1);
    if (!xc || !xc->image_count) return;
    img = xc->images[0];
    wlr_xwayland_set_cursor(server.xwayland, wlr_xcursor_image_get_buffer(img),
                            img->hotspot_x, img->hotspot_y);
}

void xwayland_update_workareas(void)
{
    struct wlr_box *boxes;
    guint i;

    if (!server.xwayland || !server.xwayland->xwm) return;
    /* _NET_WORKAREA, one per desktop, X has no per-monitor work areas */
    boxes = g_new(struct wlr_box, screen_num_desktops);
    for (i = 0; i < screen_num_desktops; ++i) {
        const Rect *a = screen_area(i, SCREEN_AREA_ALL_MONITORS, NULL);
        boxes[i].x = a->x;
        boxes[i].y = a->y;
        boxes[i].width = a->width;
        boxes[i].height = a->height;
        g_slice_free(Rect, (Rect*)a);
    }
    wlr_xwayland_set_workareas(server.xwayland, boxes, screen_num_desktops);
    g_free(boxes);
}

static void handle_ready(struct wl_listener *l, void *data)
{
    ob_debug("Xwayland is ready on DISPLAY=%s", server.xwayland->display_name);
    wlr_xwayland_set_seat(server.xwayland, server.seat);
    xwayland_update_cursor();
    xwayland_update_workareas();
}

void xwayland_init(void)
{
    if (config_xwayland == OB_XWAYLAND_OFF) return;

    server.xwayland = wlr_xwayland_create(server.display, server.compositor,
                                          config_xwayland ==
                                          OB_XWAYLAND_LAZY);
    if (!server.xwayland) {
        g_message("Unable to start Xwayland, X applications won't run");
        return;
    }
    new_surface.notify = handle_new_surface;
    wl_signal_add(&server.xwayland->events.new_surface, &new_surface);
    ready.notify = handle_ready;
    wl_signal_add(&server.xwayland->events.ready, &ready);
}

void xwayland_start(void)
{
    if (!server.xwayland) {
        g_unsetenv("DISPLAY");
        return;
    }
    /* the X socket exists already, so X clients can be run right away
       (they start Xwayland when it is lazy) */
    g_setenv("DISPLAY", server.xwayland->display_name, TRUE);
    ob_debug("Running X applications on DISPLAY=%s",
             server.xwayland->display_name);
}

gboolean xwayland_global_allowed(const struct wl_client *client,
                                 const struct wl_global *global)
{
    /* the xwayland_shell_v1 global is only for Xwayland itself */
    if (server.xwayland && server.xwayland->shell_v1 &&
        global == server.xwayland->shell_v1->global)
        return server.xwayland->server &&
            client == server.xwayland->server->client;
    return TRUE;
}

void xwayland_finish(void)
{
    if (!server.xwayland) return;
    wl_list_remove(&new_surface.link);
    wl_list_remove(&ready.link);
    wlr_xwayland_destroy(server.xwayland);
    server.xwayland = NULL;
}
