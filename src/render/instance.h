/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   instance.h for owbox (ported from the Openbox window manager)
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

#ifndef __render_instance_h
#define __render_instance_h

#include <glib.h>
#include <pango/pangocairo.h>

/* Under X the instance described the visual/colormap.  owbox always renders
   into 32-bit ARGB memory, so all that is left is the pango context used for
   measuring text and the color cache. */
struct _RrInstance {
    PangoFontMap *fontmap;
    PangoContext *pango;
    cairo_font_options_t *font_options;
    GHashTable *color_hash;
};

GHashTable* RrColorHash    (const RrInstance *inst);

#endif
