/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   buffer.c for owbox
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

#include "buffer.h"

#include <drm_fourcc.h>
#include <glib.h>
#include <wlr/interfaces/wlr_buffer.h>

typedef struct {
    struct wlr_buffer base;
    cairo_surface_t *surface;
} ObCairoBuffer;

static void buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    ObCairoBuffer *b = wl_container_of(wlr_buffer, b, base);
    wlr_buffer_finish(wlr_buffer);
    cairo_surface_destroy(b->surface);
    g_free(b);
}

static bool buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                         uint32_t flags, void **data,
                                         uint32_t *format, size_t *stride)
{
    ObCairoBuffer *b = wl_container_of(wlr_buffer, b, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;
    *data = cairo_image_surface_get_data(b->surface);
    *format = DRM_FORMAT_ARGB8888;
    *stride = cairo_image_surface_get_stride(b->surface);
    return true;
}

static void buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
    .destroy = buffer_destroy,
    .begin_data_ptr_access = buffer_begin_data_ptr_access,
    .end_data_ptr_access = buffer_end_data_ptr_access,
};

struct wlr_buffer* ob_buffer_from_cairo(cairo_surface_t *surface)
{
    ObCairoBuffer *b;

    g_assert(cairo_image_surface_get_format(surface) == CAIRO_FORMAT_ARGB32);
    cairo_surface_flush(surface);

    b = g_new0(ObCairoBuffer, 1);
    wlr_buffer_init(&b->base, &cairo_buffer_impl,
                    cairo_image_surface_get_width(surface),
                    cairo_image_surface_get_height(surface));
    b->surface = surface;
    return &b->base;
}
