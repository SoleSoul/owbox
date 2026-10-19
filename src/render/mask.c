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

/* owbox: how much of the drawn pixel (x, y) the XBM's set bits cover, when
   it is drawn at a scale */
static guchar coverage(const gchar *data, gint w, gint h, gdouble scale,
                       gint x, gint y)
{
    const gint stride = (w + 7) / 8;
    const gdouble x0 = x / scale, x1 = (x + 1) / scale;
    const gdouble y0 = y / scale, y1 = (y + 1) / scale;
    gdouble sum = 0;
    gint mx, my;

    for (my = (gint)y0; my < h && my < y1; ++my) {
        const guchar *row = (const guchar*)data + my * stride;
        gdouble dy = MIN(y1, my + 1) - MAX(y0, my);
        for (mx = (gint)x0; mx < w && mx < x1; ++mx)
            if (row[mx / 8] & (1 << (mx % 8)))
                sum += dy * (MIN(x1, mx + 1) - MAX(x0, mx));
    }
    /* the drawn pixel is 1/scale^2 of the XBM's pixels */
    sum *= scale * scale;
    return (guchar)(MIN(sum, 1.0) * 255 + 0.5);
}

RrPixmapMask *RrPixmapMaskNew(const RrInstance *inst,
                              gint w, gint h, const gchar *data)
{
    RrPixmapMask *m = g_slice_new(RrPixmapMask);
    const gdouble scale = RrScale(inst);
    m->inst = inst;
    m->width = w;
    m->height = h;
    /* round up to nearest byte */
    m->data = g_memdup2(data, (w + 7) / 8 * h);

    /* owbox: draw it larger at a scale, smoothed unless the scale is a whole
       number */
    m->xbm_width = w;
    m->xbm_height = h;
    m->alpha = NULL;
    if (scale != 1.0 && w > 0 && h > 0) {
        gint x, y;
        m->width = MAX((gint)(w * scale + 0.5), 1);
        m->height = MAX((gint)(h * scale + 0.5), 1);
        m->alpha = g_new(guchar, m->width * m->height);
        for (y = 0; y < m->height; ++y)
            for (x = 0; x < m->width; ++x)
                m->alpha[y * m->width + x] =
                    coverage(data, w, h, (gdouble)m->width / w, x, y);
    }
    return m;
}

void RrPixmapMaskFree(RrPixmapMask *m)
{
    if (m) {
        g_free(m->data);
        g_free(m->alpha);
        g_slice_free(RrPixmapMask, m);
    }
}

/* owbox: c over the pixel p, a of 255 of it */
static RrPixel32 blend(RrPixel32 p, RrPixel32 c, guint a)
{
    RrPixel32 out = p & (0xff << RrDefaultAlphaOffset);
    gint off;

    for (off = 0; off < 24; off += 8) {
        guint pc = (p >> off) & 0xff, cc = (c >> off) & 0xff;
        out |= ((pc * (255 - a) + cc * a + 127) / 255) << off;
    }
    return out;
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

    pix = RrColorPixel(m->color);
    if (m->mask->alpha) {
        /* owbox: a mask drawn at a scale */
        for (my = 0; my < m->mask->height && y + my < th; ++my) {
            const guchar *row = m->mask->alpha + my * m->mask->width;
            RrPixel32 *out = target + (y + my) * tw + x;
            for (mx = 0; mx < m->mask->width && x + mx < tw; ++mx)
                if (row[mx] == 255)
                    out[mx] = pix;
                else if (row[mx])
                    out[mx] = blend(out[mx], pix, row[mx]);
        }
        return;
    }

    /* XBM data is LSB first, rows padded to whole bytes */
    stride = (m->mask->width + 7) / 8;
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
    return RrPixmapMaskNew(src->inst, src->xbm_width, src->xbm_height,
                           src->data);
}
