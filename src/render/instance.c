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
    return inst;
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
