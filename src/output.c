/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   output.c for owbox
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

#include "output.h"
#include "server.h"
#include "screen.h"
#include "config.h"
#include "openbox.h"
#include "layers.h"
#include "seat.h"
#include "debug.h"

#include <time.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static struct wl_listener new_output;
static struct wl_listener layout_change;
static struct wl_listener manager_apply;
static struct wl_listener manager_test;
static struct wl_listener power_set_mode;

/*! The monitors: enabled outputs sorted by position */
static GPtrArray *monitors = NULL;

static gint compare_outputs(gconstpointer a, gconstpointer b)
{
    const ObOutput *oa = *(ObOutput* const*)a;
    const ObOutput *ob = *(ObOutput* const*)b;
    if (oa->area.x != ob->area.x) return oa->area.x - ob->area.x;
    return oa->area.y - ob->area.y;
}

static void update_monitors(void)
{
    ObOutput *o;

    g_ptr_array_set_size(monitors, 0);
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_box box;

        o->in_layout = o->wlr_output->enabled &&
            wlr_output_layout_get(server.output_layout, o->wlr_output);
        if (!o->in_layout) continue;

        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &box);
        RECT_SET(o->area, box.x, box.y, box.width, box.height);
        g_ptr_array_add(monitors, o);
    }
    g_ptr_array_sort(monitors, compare_outputs);
}

guint output_count(void)
{
    return monitors ? monitors->len : 0;
}

ObOutput* output_nth(guint n)
{
    g_assert(n < monitors->len);
    return g_ptr_array_index(monitors, n);
}

ObOutput* output_at(gint x, gint y)
{
    struct wlr_output *o = wlr_output_layout_output_at(server.output_layout,
                                                       x, y);
    return o ? output_from_wlr(o) : NULL;
}

ObOutput* output_from_wlr(struct wlr_output *wlr_output)
{
    ObOutput *o;
    wl_list_for_each(o, &server.outputs, link)
        if (o->wlr_output == wlr_output)
            return o;
    return NULL;
}

gfloat output_max_scale(void)
{
    gfloat scale = 1.0f;
    guint i;
    for (i = 0; monitors && i < monitors->len; ++i) {
        ObOutput *o = g_ptr_array_index(monitors, i);
        scale = MAX(scale, o->wlr_output->scale);
    }
    return scale;
}

/* Tell output management clients (wlr-randr, kanshi) about the current
   configuration */
static void update_output_manager(void)
{
    struct wlr_output_configuration_v1 *config;
    ObOutput *o;

    if (!server.output_manager) return;

    config = wlr_output_configuration_v1_create();
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, o->wlr_output);
        struct wlr_box box;

        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &box);
        head->state.enabled = o->wlr_output->enabled;
        if (!wlr_box_empty(&box)) {
            head->state.x = box.x;
            head->state.y = box.y;
        }
    }
    wlr_output_manager_v1_set_configuration(server.output_manager, config);
}

static void handle_layout_change(struct wl_listener *listener, void *data)
{
    update_monitors();
    update_output_manager();

    if (server.root_rect) {
        struct wlr_box box;
        wlr_output_layout_get_box(server.output_layout, NULL, &box);
        wlr_scene_node_set_position(&server.root_rect->node, box.x, box.y);
        wlr_scene_rect_set_size(server.root_rect, box.width, box.height);
    }

    layers_arrange_all();

    if (ob_state() == OB_STATE_RUNNING ||
        ob_state() == OB_STATE_RECONFIGURING)
        screen_resize();

    seat_output_layout_changed();
}

static void output_frame(struct wl_listener *listener, void *data)
{
    ObOutput *output = wl_container_of(listener, output, frame);
    struct timespec now;

    if (!output->scene_output) return;
    wlr_scene_output_commit(output->scene_output, NULL);

    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    ObOutput *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    ObOutput *output = wl_container_of(listener, output, destroy);

    layers_output_destroyed(output);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    g_free(output);

    /* the layout's change signal updates the monitors */
}

/* Find the rc.xml settings for an output */
static ObOutputConfig* find_config(struct wlr_output *wlr_output)
{
    GSList *it;
    for (it = config_outputs; it; it = g_slist_next(it)) {
        ObOutputConfig *oc = it->data;
        if (oc->name && (g_pattern_match_simple(oc->name, wlr_output->name) ||
                         (wlr_output->description &&
                          g_pattern_match_simple(oc->name,
                                                 wlr_output->description))))
            return oc;
    }
    return NULL;
}

static struct wlr_output_mode* find_mode(struct wlr_output *o,
                                         ObOutputConfig *oc)
{
    struct wlr_output_mode *m, *best = NULL;

    wl_list_for_each(m, &o->modes, link) {
        if (m->width != oc->width || m->height != oc->height)
            continue;
        if (oc->refresh) {
            if (abs(m->refresh - oc->refresh) < 1000 &&
                (!best || abs(m->refresh - oc->refresh) <
                 abs(best->refresh - oc->refresh)))
                best = m;
        } else if (!best || m->refresh > best->refresh)
            best = m;
    }
    return best;
}

/* Set up an output's state from the configuration (or the defaults), and
   put it in the layout */
static void configure_output(ObOutput *output)
{
    struct wlr_output *o = output->wlr_output;
    ObOutputConfig *oc = find_config(o);
    struct wlr_output_state state;
    gboolean enable = !oc || oc->enabled != 0;

    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enable);
    if (enable) {
        struct wlr_output_mode *mode = NULL;

        if (oc && oc->width > 0 && oc->height > 0) {
            mode = find_mode(o, oc);
            if (!mode && wl_list_empty(&o->modes))
                wlr_output_state_set_custom_mode(&state, oc->width,
                                                 oc->height, oc->refresh);
            else if (!mode)
                g_message("Output %s has no mode %dx%d, using its "
                          "preferred mode", o->name, oc->width, oc->height);
        }
        if (!mode && !wl_list_empty(&o->modes))
            mode = wlr_output_preferred_mode(o);
        if (mode)
            wlr_output_state_set_mode(&state, mode);

        if (oc && oc->scale > 0)
            wlr_output_state_set_scale(&state, oc->scale);
        if (oc && oc->transform >= 0)
            wlr_output_state_set_transform(&state, oc->transform);
        if (oc && oc->adaptive_sync >= 0)
            wlr_output_state_set_adaptive_sync_enabled(&state,
                                                       oc->adaptive_sync);
    }

    if (!wlr_output_commit_state(o, &state) && enable) {
        /* try again with only the preferred mode */
        struct wlr_output_mode *mode = wlr_output_preferred_mode(o);
        g_message("Unable to apply the configuration for output %s",
                  o->name);
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, TRUE);
        if (mode)
            wlr_output_state_set_mode(&state, mode);
        wlr_output_commit_state(o, &state);
    }
    wlr_output_state_finish(&state);

    if (o->enabled) {
        struct wlr_output_layout_output *lo;

        if (oc && oc->pos_set)
            lo = wlr_output_layout_add(server.output_layout, o,
                                       oc->x, oc->y);
        else
            lo = wlr_output_layout_add_auto(server.output_layout, o);
        if (!output->scene_output)
            output->scene_output = wlr_scene_output_create(server.scene, o);
        wlr_scene_output_layout_add_output(server.scene_layout, lo,
                                           output->scene_output);
    } else {
        wlr_output_layout_remove(server.output_layout, o);
        if (output->scene_output) {
            wlr_scene_output_destroy(output->scene_output);
            output->scene_output = NULL;
        }
    }
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct wlr_output *wlr_output = data;
    ObOutput *output;

    if (!wlr_output_init_render(wlr_output, server.allocator,
                                server.renderer))
    {
        g_message("Unable to initialize rendering for output %s",
                  wlr_output->name);
        return;
    }

    output = g_new0(ObOutput, 1);
    output->wlr_output = wlr_output;
    wlr_output->data = output;
    wl_list_insert(server.outputs.prev, &output->link);

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    ob_debug("New output %s (%s)", wlr_output->name,
             wlr_output->description ? wlr_output->description : "");

    configure_output(output);
}

void output_reconfigure(void)
{
    ObOutput *o;
    wl_list_for_each(o, &server.outputs, link)
        configure_output(o);
}

/* Apply (or test) a configuration from an output management client */
static void apply_manager_config(struct wlr_output_configuration_v1 *config,
                                 gboolean test)
{
    struct wlr_output_configuration_head_v1 *head;
    gboolean ok = TRUE;

    wl_list_for_each(head, &config->heads, link) {
        struct wlr_output *o = head->state.output;
        struct wlr_output_state state;

        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&head->state, &state);
        if (test)
            ok = ok && wlr_output_test_state(o, &state);
        else
            ok = ok && wlr_output_commit_state(o, &state);
        wlr_output_state_finish(&state);
    }

    if (!test && ok) {
        wl_list_for_each(head, &config->heads, link) {
            struct wlr_output *o = head->state.output;
            ObOutput *output = output_from_wlr(o);

            if (!output) continue;
            if (head->state.enabled) {
                struct wlr_output_layout_output *lo =
                    wlr_output_layout_add(server.output_layout, o,
                                          head->state.x, head->state.y);
                if (!output->scene_output)
                    output->scene_output =
                        wlr_scene_output_create(server.scene, o);
                wlr_scene_output_layout_add_output(server.scene_layout, lo,
                                                   output->scene_output);
            } else {
                wlr_output_layout_remove(server.output_layout, o);
                if (output->scene_output) {
                    wlr_scene_output_destroy(output->scene_output);
                    output->scene_output = NULL;
                }
            }
        }
    }

    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);

    if (!test)
        update_output_manager();
}

static void handle_manager_apply(struct wl_listener *listener, void *data)
{
    apply_manager_config(data, FALSE);
}

static void handle_manager_test(struct wl_listener *listener, void *data)
{
    apply_manager_config(data, TRUE);
}

static void handle_power_set_mode(struct wl_listener *listener, void *data)
{
    struct wlr_output_power_v1_set_mode_event *event = data;
    struct wlr_output_state state;

    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state,
                                 event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);
}

void output_init(void)
{
    monitors = g_ptr_array_new();
    wl_list_init(&server.outputs);

    new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &new_output);

    layout_change.notify = handle_layout_change;
    wl_signal_add(&server.output_layout->events.change, &layout_change);

    server.output_manager = wlr_output_manager_v1_create(server.display);
    manager_apply.notify = handle_manager_apply;
    wl_signal_add(&server.output_manager->events.apply, &manager_apply);
    manager_test.notify = handle_manager_test;
    wl_signal_add(&server.output_manager->events.test, &manager_test);

    server.output_power = wlr_output_power_manager_v1_create(server.display);
    power_set_mode.notify = handle_power_set_mode;
    wl_signal_add(&server.output_power->events.set_mode, &power_set_mode);
}

void output_finish(void)
{
    wl_list_remove(&new_output.link);
    wl_list_remove(&layout_change.link);
    wl_list_remove(&manager_apply.link);
    wl_list_remove(&manager_test.link);
    wl_list_remove(&power_set_mode.link);
    g_ptr_array_free(monitors, TRUE);
    monitors = NULL;
}
