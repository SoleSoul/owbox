/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   instance.c for owbox (ported from the Openbox window manager)
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

#include "render.h"
#include "instance.h"

RrInstance* RrInstanceNew(void)
{
    RrInstance *inst = g_slice_new0(RrInstance);

    inst->fontmap = pango_cairo_font_map_new();
    /* Xft's default resolution, so that point sizes in rc.xml match what
       Openbox shows */
    pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(inst->fontmap),
                                        96.0);
    inst->pango = pango_font_map_create_context(inst->fontmap);

    inst->font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(inst->font_options,
                                     CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(inst->font_options,
                                      CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(inst->font_options,
                                        CAIRO_HINT_METRICS_ON);
    pango_cairo_context_set_font_options(inst->pango, inst->font_options);

    inst->color_hash = g_hash_table_new_full(g_int_hash, g_int_equal,
                                             NULL, NULL);
    inst->scale = 1.0;
    return inst;
}

/* owbox: draw everything larger by a factor: the theme's sizes, fonts (at a
   higher resolution), masks and lines.  It applies to fonts and themes that
   are loaded after it. */
void RrInstanceSetScale(RrInstance *inst, gdouble scale)
{
    inst->scale = scale;
    pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(inst->fontmap),
                                        96.0 * scale);
    pango_context_changed(inst->pango);
}

gdouble RrScale(const RrInstance *inst)
{
    return inst->scale;
}

/* a size in the theme's pixels, in pixels to draw, rounded, but at least one
   pixel when it was at least one */
gint RrScaleInt(const RrInstance *inst, gint v)
{
    gint r;

    if (inst->scale == 1.0 || v == 0) return v;
    r = (gint)(ABS(v) * inst->scale + 0.5);
    r = MAX(r, 1);
    return v < 0 ? -r : r;
}

/* the width of the theme's 1 pixel lines (bevels, borders) */
gint RrLineWidth(const RrInstance *inst)
{
    return RrScaleInt(inst, 1);
}

void RrInstanceFree(RrInstance *inst)
{
    if (inst) {
        g_hash_table_destroy(inst->color_hash);
        cairo_font_options_destroy(inst->font_options);
        g_object_unref(inst->pango);
        g_object_unref(inst->fontmap);
        g_slice_free(RrInstance, inst);
    }
}

GHashTable* RrColorHash(const RrInstance *inst)
{
    return inst->color_hash;
}
