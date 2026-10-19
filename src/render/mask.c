/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   mask.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2003-2007   Dana Jansens
   Copyright (c) 2003        Derek Foreman

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
#include "color.h"
#include "mask.h"

RrPixmapMask *RrPixmapMaskNew(const RrInstance *inst,
                              gint w, gint h, const gchar *data)
{
    RrPixmapMask *m = g_slice_new(RrPixmapMask);
    m->inst = inst;
    m->width = w;
    m->height = h;
    /* round up to nearest byte */
    m->data = g_memdup2(data, (w + 7) / 8 * h);
    return m;
}

void RrPixmapMaskFree(RrPixmapMask *m)
{
    if (m) {
        g_free(m->data);
        g_slice_free(RrPixmapMask, m);
    }
}

void RrPixmapMaskDraw(RrPixel32 *target, gint tw, gint th,
                      const RrTextureMask *m, const RrRect *area)
{
    gint x, y, mx, my, stride;
    RrPixel32 pix;
    if (m->mask == NULL) return; /* no mask given */

    /* set the clip region */
    x = area->x + (area->width - m->mask->width) / 2;
    y = area->y + (area->height - m->mask->height) / 2;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    /* XBM data is LSB first, rows padded to whole bytes */
    stride = (m->mask->width + 7) / 8;
    pix = RrColorPixel(m->color);
    for (my = 0; my < m->mask->height && y + my < th; ++my) {
        const guchar *row = (const guchar*)m->mask->data + my * stride;
        RrPixel32 *out = target + (y + my) * tw + x;
        for (mx = 0; mx < m->mask->width && x + mx < tw; ++mx)
            if (row[mx / 8] & (1 << (mx % 8)))
                out[mx] = pix;
    }
}

RrPixmapMask *RrPixmapMaskCopy(const RrPixmapMask *src)
{
    return RrPixmapMaskNew(src->inst, src->width, src->height, src->data);
}
