/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   tablet.c for owbox
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

#include "tablet.h"
#include "seat.h"
#include "server.h"
#include "debug.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>

typedef struct {
    struct wlr_input_device *device;
    struct wlr_tablet_v2_tablet *v2;
    struct wl_listener destroy;
} ObTablet;

typedef struct {
    struct wlr_tablet_v2_tablet_tool *v2;
    /*! wlroots' events have only the tilt axes that changed, the clients
      get both */
    gdouble tilt_x, tilt_y;
    struct wl_listener set_cursor;
    struct wl_listener destroy;
} ObTabletTool;

typedef struct {
    struct wlr_tablet_v2_tablet_pad *v2;
    /*! The tablet it belongs to, for the clients */
    ObTablet *tablet;
    /*! The surface it sends to, the focused window's */
    struct wlr_surface *focus;
    struct wl_listener focus_destroy;
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;
    struct wl_listener destroy;
} ObTabletPad;

static struct wlr_tablet_manager_v2 *manager = NULL;
static GList *tablets = NULL; /* ObTablet* */
static GList *tools = NULL;   /* ObTabletTool* */
static GList *pads = NULL;    /* ObTabletPad* */

static struct wl_listener tool_axis;
static struct wl_listener tool_proximity;
static struct wl_listener tool_tip;
static struct wl_listener tool_button;
static struct wl_listener keyboard_focus;

/* ---- tools ---- */

static void tool_set_cursor(struct wl_listener *l, void *data)
{
    ObTabletTool *t = wl_container_of(l, t, set_cursor);
    struct wlr_tablet_v2_event_cursor *ev = data;
    struct wlr_surface *s = t->v2->focused_surface;

    /* only the client the tool is over */
    if (!s || !ev->seat_client ||
        wl_resource_get_client(s->resource) != ev->seat_client->client)
        return;
    seat_tool_set_cursor(ev->surface, ev->hotspot_x, ev->hotspot_y);
}

static void tool_destroy(struct wl_listener *l, void *data)
{
    ObTabletTool *t = wl_container_of(l, t, destroy);

    wl_list_remove(&t->set_cursor.link);
    wl_list_remove(&t->destroy.link);
    tools = g_list_remove(tools, t);
    g_free(t);
}

/* wlroots tells about a tool first in its events */
static ObTabletTool* tool_get(struct wlr_tablet_tool *wlr)
{
    ObTabletTool *t = wlr->data;

    if (t) return t;
    t = g_new0(ObTabletTool, 1);
    t->v2 = wlr_tablet_tool_create(manager, server.seat, wlr);
    wlr->data = t;
    t->set_cursor.notify = tool_set_cursor;
    wl_signal_add(&t->v2->events.set_cursor, &t->set_cursor);
    t->destroy.notify = tool_destroy;
    wl_signal_add(&wlr->events.destroy, &t->destroy);
    tools = g_list_prepend(tools, t);
    ob_debug("Tablet tool%s%s%s%s%s%s",
             wlr->pressure ? " pressure" : "", wlr->tilt ? " tilt" : "",
             wlr->distance ? " distance" : "",
             wlr->rotation ? " rotation" : "", wlr->slider ? " slider" : "",
             wlr->wheel ? " wheel" : "");
    return t;
}

/* The tool that talks to clients with the tablet protocol, or NULL for
   one that is only a pointer: a tablet's mouse or lens, which clients
   rarely handle as tablet tools.  Those move like a mouse. */
static struct wlr_tablet_v2_tablet_tool* tool_v2(ObTabletTool *t)
{
    switch (t->v2->wlr_tool->type) {
    case WLR_TABLET_TOOL_TYPE_MOUSE:
    case WLR_TABLET_TOOL_TYPE_LENS:
        return NULL;
    default:
        return t->v2;
    }
}

static void handle_tool_proximity(struct wl_listener *l, void *data)
{
    struct wlr_tablet_tool_proximity_event *ev = data;
    ObTablet *tb = ev->tablet->data;
    ObTabletTool *t = tool_get(ev->tool);
    struct wlr_tablet_v2_tablet_tool *v2 = tool_v2(t);

    if (!tb) return;
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
        t->tilt_x = t->tilt_y = 0;
        if (v2)
            wlr_cursor_warp_absolute(server.cursor, &ev->tablet->base,
                                     ev->x, ev->y);
        seat_tool_motion(v2, tb->v2, ev->time_msec);
    } else {
        if (t->v2->focused_surface)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
        /* the pointer is where the tool left it */
        seat_rebase_pointer();
    }
}

static void handle_tool_axis(struct wl_listener *l, void *data)
{
    struct wlr_tablet_tool_axis_event *ev = data;
    ObTablet *tb = ev->tablet->data;
    ObTabletTool *t = tool_get(ev->tool);
    struct wlr_tablet_v2_tablet_tool *v2 = tool_v2(t);
    guint32 axes = ev->updated_axes;

    if (!tb) return;
    if (axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y)) {
        if (v2)
            wlr_cursor_warp_absolute(
                server.cursor, &ev->tablet->base,
                axes & WLR_TABLET_TOOL_AXIS_X ? ev->x : NAN,
                axes & WLR_TABLET_TOOL_AXIS_Y ? ev->y : NAN);
        else
            wlr_cursor_move(server.cursor, &ev->tablet->base, ev->dx, ev->dy);
        seat_tool_motion(v2, tb->v2, ev->time_msec);
    }

    if (axes & WLR_TABLET_TOOL_AXIS_TILT_X)
        t->tilt_x = ev->tilt_x;
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
        t->tilt_y = ev->tilt_y;

    /* the rest is for a client that takes the tablet protocol */
    if (!v2 || !v2->focused_surface) return;
    if (axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
        wlr_tablet_v2_tablet_tool_notify_pressure(v2, ev->pressure);
    if (axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
        wlr_tablet_v2_tablet_tool_notify_distance(v2, ev->distance);
    if (axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
        wlr_tablet_v2_tablet_tool_notify_tilt(v2, t->tilt_x, t->tilt_y);
    if (axes & WLR_TABLET_TOOL_AXIS_ROTATION)
        wlr_tablet_v2_tablet_tool_notify_rotation(v2, ev->rotation);
    if (axes & WLR_TABLET_TOOL_AXIS_SLIDER)
        wlr_tablet_v2_tablet_tool_notify_slider(v2, ev->slider);
    if (axes & WLR_TABLET_TOOL_AXIS_WHEEL)
        wlr_tablet_v2_tablet_tool_notify_wheel(v2, ev->wheel_delta, 0);
}

/* the tip is the left button */
static void handle_tool_tip(struct wl_listener *l, void *data)
{
    struct wlr_tablet_tool_tip_event *ev = data;
    ObTablet *tb = ev->tablet->data;
    ObTabletTool *t = tool_get(ev->tool);

    if (!tb) return;
    seat_tool_button(tool_v2(t), tb->v2, ev->time_msec, BTN_LEFT,
                     ev->state == WLR_TABLET_TOOL_TIP_DOWN);
}

/* the stylus buttons are middle and right for owbox and clients without
   the tablet protocol, as in X */
static void handle_tool_button(struct wl_listener *l, void *data)
{
    struct wlr_tablet_tool_button_event *ev = data;
    ObTablet *tb = ev->tablet->data;
    ObTabletTool *t = tool_get(ev->tool);
    guint32 button;

    if (!tb) return;
    switch (ev->button) {
    case BTN_STYLUS:  button = BTN_MIDDLE; break;
    case BTN_STYLUS2: button = BTN_RIGHT;  break;
    case BTN_STYLUS3: button = BTN_SIDE;   break;
    default:          button = ev->button; break; /* a tablet mouse's */
    }
    seat_tool_button(tool_v2(t), tb->v2, ev->time_msec, button,
                     ev->state == WLR_BUTTON_PRESSED);
}

void tablet_tools_leave(void)
{
    GList *it;

    for (it = tools; it; it = g_list_next(it)) {
        ObTabletTool *t = it->data;
        if (t->v2->focused_surface)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
    }
}

/* ---- pads ---- */

static void pad_set_focus(ObTabletPad *p, struct wlr_surface *surface)
{
    if (surface && (!p->tablet ||
                    !wlr_surface_accepts_tablet_v2(surface, p->tablet->v2)))
        surface = NULL;
    if (surface == p->focus) return;
    if (p->focus) {
        wlr_tablet_v2_tablet_pad_notify_leave(p->v2, p->focus);
        wl_list_remove(&p->focus_destroy.link);
    }
    p->focus = surface;
    if (surface) {
        wlr_tablet_v2_tablet_pad_notify_enter(p->v2, p->tablet->v2, surface);
        wl_signal_add(&surface->events.destroy, &p->focus_destroy);
    }
}

static void pad_focus_destroy(struct wl_listener *l, void *data)
{
    ObTabletPad *p = wl_container_of(l, p, focus_destroy);
    wl_list_remove(&p->focus_destroy.link);
    p->focus = NULL;
}

/* a pad goes with a tablet, the last one plugged in */
static void pads_attach(void)
{
    GList *it;

    for (it = pads; it; it = g_list_next(it)) {
        ObTabletPad *p = it->data;
        ObTablet *tb = tablets ? g_list_last(tablets)->data : NULL;
        if (p->tablet == tb) continue;
        pad_set_focus(p, NULL);
        p->tablet = tb;
        pad_set_focus(p, server.seat->keyboard_state.focused_surface);
    }
}

static void handle_keyboard_focus(struct wl_listener *l, void *data)
{
    struct wlr_seat_keyboard_focus_change_event *ev = data;
    GList *it;

    for (it = pads; it; it = g_list_next(it))
        pad_set_focus(it->data, ev->new_surface);
}

static void pad_button(struct wl_listener *l, void *data)
{
    ObTabletPad *p = wl_container_of(l, p, button);
    struct wlr_tablet_pad_button_event *ev = data;

    if (!p->focus) return;
    wlr_tablet_v2_tablet_pad_notify_mode(p->v2, ev->group, ev->mode,
                                         ev->time_msec);
    wlr_tablet_v2_tablet_pad_notify_button(
        p->v2, ev->button, ev->time_msec,
        ev->state == WLR_BUTTON_PRESSED ?
        ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED :
        ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
}

static void pad_ring(struct wl_listener *l, void *data)
{
    ObTabletPad *p = wl_container_of(l, p, ring);
    struct wlr_tablet_pad_ring_event *ev = data;

    if (!p->focus) return;
    wlr_tablet_v2_tablet_pad_notify_ring(
        p->v2, ev->ring, ev->position,
        ev->source == WLR_TABLET_PAD_RING_SOURCE_FINGER, ev->time_msec);
}

static void pad_strip(struct wl_listener *l, void *data)
{
    ObTabletPad *p = wl_container_of(l, p, strip);
    struct wlr_tablet_pad_strip_event *ev = data;

    if (!p->focus) return;
    wlr_tablet_v2_tablet_pad_notify_strip(
        p->v2, ev->strip, ev->position,
        ev->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER, ev->time_msec);
}

static void pad_destroy(struct wl_listener *l, void *data)
{
    ObTabletPad *p = wl_container_of(l, p, destroy);

    if (p->focus)
        wl_list_remove(&p->focus_destroy.link);
    wl_list_remove(&p->button.link);
    wl_list_remove(&p->ring.link);
    wl_list_remove(&p->strip.link);
    wl_list_remove(&p->destroy.link);
    pads = g_list_remove(pads, p);
    g_free(p);
}

void tablet_pad_new(struct wlr_input_device *device)
{
    struct wlr_tablet_pad *wlr = wlr_tablet_pad_from_input_device(device);
    ObTabletPad *p = g_new0(ObTabletPad, 1);

    ob_debug("Tablet pad %s", device->name ? device->name : "");
    p->v2 = wlr_tablet_pad_create(manager, server.seat, device);
    p->focus_destroy.notify = pad_focus_destroy;
    p->button.notify = pad_button;
    wl_signal_add(&wlr->events.button, &p->button);
    p->ring.notify = pad_ring;
    wl_signal_add(&wlr->events.ring, &p->ring);
    p->strip.notify = pad_strip;
    wl_signal_add(&wlr->events.strip, &p->strip);
    p->destroy.notify = pad_destroy;
    wl_signal_add(&device->events.destroy, &p->destroy);
    pads = g_list_append(pads, p);
    pads_attach();
}

/* ---- tablets ---- */

static void tablet_destroy(struct wl_listener *l, void *data)
{
    ObTablet *tb = wl_container_of(l, tb, destroy);

    wl_list_remove(&tb->destroy.link);
    tablets = g_list_remove(tablets, tb);
    g_free(tb);
    pads_attach();
}

void tablet_new(struct wlr_input_device *device)
{
    ObTablet *tb = g_new0(ObTablet, 1);
    struct wlr_tablet *wlr = wlr_tablet_from_input_device(device);

    ob_debug("Tablet %s, %.0fx%.0f mm", device->name ? device->name : "",
             wlr->width_mm, wlr->height_mm);
    tb->device = device;
    tb->v2 = wlr_tablet_create(manager, server.seat, device);
    wlr->data = tb;
    tb->destroy.notify = tablet_destroy;
    wl_signal_add(&device->events.destroy, &tb->destroy);
    tablets = g_list_append(tablets, tb);
    /* the tablet covers all the monitors, as the pointer does */
    wlr_cursor_attach_input_device(server.cursor, device);
    pads_attach();
}

/* ---- setup ---- */

void tablet_init(void)
{
    manager = wlr_tablet_v2_create(server.display);

    tool_axis.notify = handle_tool_axis;
    wl_signal_add(&server.cursor->events.tablet_tool_axis, &tool_axis);
    tool_proximity.notify = handle_tool_proximity;
    wl_signal_add(&server.cursor->events.tablet_tool_proximity,
                  &tool_proximity);
    tool_tip.notify = handle_tool_tip;
    wl_signal_add(&server.cursor->events.tablet_tool_tip, &tool_tip);
    tool_button.notify = handle_tool_button;
    wl_signal_add(&server.cursor->events.tablet_tool_button, &tool_button);
    keyboard_focus.notify = handle_keyboard_focus;
    wl_signal_add(&server.seat->keyboard_state.events.focus_change,
                  &keyboard_focus);
}

void tablet_finish(void)
{
    wl_list_remove(&tool_axis.link);
    wl_list_remove(&tool_proximity.link);
    wl_list_remove(&tool_tip.link);
    wl_list_remove(&tool_button.link);
    wl_list_remove(&keyboard_focus.link);
}
