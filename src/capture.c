/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   capture.c for owbox
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

/* Screen capture (ext-image-copy-capture) of monitors and windows, for
   screenshots and screen sharing.

   wlroots offers the clients one format for copying into memory: the one
   the GPU reads fastest.  NVIDIA's driver says that is 24 bit RGB, which
   screen sharing (PipeWire consumers like browsers) doesn't take, so they
   get nothing.  The 32 bit formats are offered too, the renderer reads
   them as well. */

#include "capture.h"
#include "server.h"

#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>

typedef struct {
    struct wlr_ext_image_capture_source_v1 *source;
    struct wl_listener constraints_update;
    struct wl_listener destroy;
} ObCaptureSource;

static struct wl_listener new_session;
/*! ObCaptureSource for each source that has had a session */
static GHashTable *sources = NULL;

static const uint32_t extra_formats[] = {
    DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888
};

/*! Add the 32 bit formats to the source's, returns TRUE if they were not
  there */
static gboolean add_formats(struct wlr_ext_image_capture_source_v1 *source)
{
    uint32_t *formats;
    size_t len = 0, i, j;
    gboolean added = FALSE;

    formats = calloc(source->shm_formats_len + G_N_ELEMENTS(extra_formats),
                     sizeof(uint32_t));
    if (!formats) return FALSE;
    /* the common ones first, clients tend to take the first they know */
    for (i = 0; i < G_N_ELEMENTS(extra_formats); ++i) {
        gboolean have = FALSE;
        for (j = 0; j < source->shm_formats_len; ++j)
            if (source->shm_formats[j] == extra_formats[i])
                have = TRUE;
        if (!have) added = TRUE;
        formats[len++] = extra_formats[i];
    }
    for (j = 0; j < source->shm_formats_len; ++j) {
        gboolean dup = FALSE;
        for (i = 0; i < G_N_ELEMENTS(extra_formats); ++i)
            if (source->shm_formats[j] == extra_formats[i])
                dup = TRUE;
        if (!dup)
            formats[len++] = source->shm_formats[j];
    }

    if (!added || source->shm_formats_len == 0) {
        /* nothing to add, or no memory copies at all (they need a renderer
           that can read the source) */
        free(formats);
        return FALSE;
    }
    free(source->shm_formats);
    source->shm_formats = formats;
    source->shm_formats_len = len;
    return TRUE;
}

static void handle_constraints_update(struct wl_listener *l, void *data)
{
    ObCaptureSource *s = wl_container_of(l, s, constraints_update);

    /* wlroots set the formats again, the sessions (listening before this)
       sent them, fix them and have the sessions send them again */
    if (add_formats(s->source))
        wl_signal_emit_mutable(&s->source->events.constraints_update, NULL);
}

static void handle_destroy(struct wl_listener *l, void *data)
{
    ObCaptureSource *s = wl_container_of(l, s, destroy);
    wl_list_remove(&s->constraints_update.link);
    wl_list_remove(&s->destroy.link);
    g_hash_table_remove(sources, s->source);
    g_free(s);
}

static void handle_new_session(struct wl_listener *l, void *data)
{
    struct wlr_ext_image_copy_capture_session_v1 *session = data;
    struct wlr_ext_image_capture_source_v1 *source = session->source;
    ObCaptureSource *s;

    if (!g_hash_table_lookup(sources, source)) {
        s = g_new0(ObCaptureSource, 1);
        s->source = source;
        s->constraints_update.notify = handle_constraints_update;
        wl_signal_add(&source->events.constraints_update,
                      &s->constraints_update);
        s->destroy.notify = handle_destroy;
        wl_signal_add(&source->events.destroy, &s->destroy);
        g_hash_table_insert(sources, source, s);
    }
    /* the session has sent the formats already, send them again */
    if (add_formats(source))
        wl_signal_emit_mutable(&source->events.constraints_update, NULL);
}

void capture_init(void)
{
    struct wlr_ext_image_copy_capture_manager_v1 *manager;

    sources = g_hash_table_new(g_direct_hash, g_direct_equal);
    wlr_ext_output_image_capture_source_manager_v1_create(server.display, 1);
    manager = wlr_ext_image_copy_capture_manager_v1_create(server.display, 1);
    new_session.notify = handle_new_session;
    wl_signal_add(&manager->events.new_session, &new_session);
}

void capture_finish(void)
{
    wl_list_remove(&new_session.link);
}
