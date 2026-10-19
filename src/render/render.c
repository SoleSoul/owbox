/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   render.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2006        Mikael Magnusson
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
#include "gradient.h"
#include "font.h"
#include "mask.h"
#include "color.h"
#include "image.h"
#include "theme.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static gboolean prepare(RrAppearance *a, gint w, gint h)
{
    if (w <= 0 || h <= 0) return FALSE;

    if (a->surface.parentx < 0 || a->surface.parenty < 0) {
        /* ob_debug("Invalid parent co-ordinates\n"); */
        return FALSE;
    }

    if (a->surface.grad == RR_SURFACE_PARENTREL &&
        (a->surface.parentx >= a->surface.parent->w ||
         a->surface.parenty >= a->surface.parent->h))
    {
        return FALSE;
    }

    if (a->w != w || a->h != h) {
        g_free(a->surface.pixel_data);
        /* zeroed so that nothing random shows where a ParentRelative
           texture is bigger than its parent */
        a->surface.pixel_data = g_new0(RrPixel32, w * h);
    }
    a->w = w;
    a->h = h;
    return TRUE;
}

void RrPaintBackground(RrAppearance *a, gint w, gint h)
{
    if (prepare(a, w, h))
        RrRender(a, w, h);
}

/* Copy the background into the output surface.  The gradient code doesn't
   set the alpha channel (X ignores it on 24-bit visuals) so make it opaque
   here. */
static void pixel_data_to_surface(RrAppearance *a, cairo_surface_t *s,
                                  gboolean force_transfer)
{
    guchar *dst = cairo_image_surface_get_data(s);
    gint stride = cairo_image_surface_get_stride(s);
    RrPixel32 *src = a->surface.pixel_data;
    gint x, y;

    cairo_surface_flush(s);
    if (a->surface.grad == RR_SURFACE_SOLID && !a->surface.interlaced &&
        !force_transfer)
    {
        /* Openbox never transfers the pixel data for these, it draws them
           with X calls instead */
        g_assert(stride == a->w * 4);
        RrRenderSolid(a, (RrPixel32*)dst, a->w, a->h);
        cairo_surface_mark_dirty(s);
        return;
    }
    for (y = 0; y < a->h; ++y) {
        guint32 *row = (guint32*)(dst + y * stride);
        for (x = 0; x < a->w; ++x)
            row[x] = *(src++) | (0xffu << RrDefaultAlphaOffset);
    }
    cairo_surface_mark_dirty(s);
}

static void draw_mask(cairo_surface_t *s, RrTextureMask *m, RrRect *area)
{
    /* masks are drawn straight into the pixels, they are 1-bit so there is
       nothing to blend */
    cairo_surface_flush(s);
    g_assert(cairo_image_surface_get_stride(s) ==
             cairo_image_surface_get_width(s) * 4);
    RrPixmapMaskDraw((RrPixel32*)cairo_image_surface_get_data(s),
                     cairo_image_surface_get_width(s),
                     cairo_image_surface_get_height(s),
                     m, area);
    cairo_surface_mark_dirty(s);
}

static void draw_line(cairo_t *cr, RrTextureLineArt *l, gint lw,
                      gint w, gint h)
{
    /* emulate XDrawLine with a 1px wide projecting cap */
    gint x1 = MIN(l->x1, l->x2), x2 = MAX(l->x1, l->x2);
    gint y1 = MIN(l->y1, l->y2), y2 = MAX(l->y1, l->y2);
    cairo_set_source_rgb(cr, l->color->r / 255.0, l->color->g / 255.0,
                         l->color->b / 255.0);
    if (x1 == x2 || y1 == y2) {
        /* owbox: lines lw wide at a scale, growing toward the middle, as
           they are drawn at the edges */
        if (y1 == y2 && y1 >= h / 2) y1 -= lw - 1;
        else if (y1 == y2) y2 += lw - 1;
        if (x1 == x2 && x1 >= w / 2) x1 -= lw - 1;
        else if (x1 == x2) x2 += lw - 1;
        cairo_rectangle(cr, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
    }
    else {
        cairo_move_to(cr, l->x1 + 0.5, l->y1 + 0.5);
        cairo_line_to(cr, l->x2 + 0.5, l->y2 + 0.5);
        cairo_set_line_width(cr, lw);
        cairo_stroke(cr);
        return;
    }
    cairo_fill(cr);
}

cairo_surface_t* RrPaintSurface(RrAppearance *a, gint w, gint h)
{
    gint i, transferred = 0, force_transfer = 0;
    RrRect tarea; /* area in which to draw textures */
    cairo_surface_t *s;
    cairo_t *cr = NULL;

    if (!prepare(a, w, h)) return NULL;

    s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);

    RrRender(a, w, h);

    {
        gint l, t, r, b;
        RrMargins(a, &l, &t, &r, &b);
        RECT_SET(tarea, l, t, w - l - r, h - t - b);
    }

    for (i = 0; i < a->textures; i++) {
        switch (a->texture[i].type) {
        case RR_TEXTURE_NONE:
            break;
        case RR_TEXTURE_TEXT:
            if (!transferred) {
                transferred = 1;
                pixel_data_to_surface(a, s, force_transfer);
            }
            if (!cr) cr = cairo_create(s);
            RrFontDraw(cr, &a->texture[i].data.text, &tarea);
            break;
        case RR_TEXTURE_LINE_ART:
            if (!transferred) {
                transferred = 1;
                pixel_data_to_surface(a, s, force_transfer);
            }
            if (!cr) cr = cairo_create(s);
            draw_line(cr, &a->texture[i].data.lineart, RrLineWidth(a->inst),
                      w, h);
            break;
        case RR_TEXTURE_MASK:
            if (!transferred) {
                transferred = 1;
                pixel_data_to_surface(a, s, force_transfer);
            }
            draw_mask(s, &a->texture[i].data.mask, &tarea);
            break;
        case RR_TEXTURE_IMAGE:
            g_assert(!transferred);
            {
                RrRect narea = tarea;
                RrTextureImage *img = &a->texture[i].data.image;
                narea.x += img->tx;
                narea.width -= img->tx;
                narea.y += img->ty;
                narea.height -= img->ty;
                if (img->twidth)
                    narea.width = MIN(narea.width, img->twidth);
                if (img->theight)
                    narea.height = MIN(narea.height, img->theight);
                RrImageDrawImage(a->surface.pixel_data,
                                 &a->texture[i].data.image,
                                 a->w, a->h,
                                 &narea);
            }
            force_transfer = 1;
            break;
        case RR_TEXTURE_RGBA:
            g_assert(!transferred);
            {
                RrRect narea = tarea;
                RrTextureRGBA *rgb = &a->texture[i].data.rgba;
                narea.x += rgb->tx;
                narea.width -= rgb->tx;
                narea.y += rgb->ty;
                narea.height -= rgb->ty;
                if (rgb->twidth)
                    narea.width = MIN(narea.width, rgb->twidth);
                if (rgb->theight)
                    narea.height = MIN(narea.height, rgb->theight);
                RrImageDrawRGBA(a->surface.pixel_data,
                                &a->texture[i].data.rgba,
                                a->w, a->h,
                                &narea);
            }
            force_transfer = 1;
        break;
        case RR_TEXTURE_NUM_TYPES:
            g_assert_not_reached();
        }
    }

    if (cr) cairo_destroy(cr);
    if (!transferred)
        pixel_data_to_surface(a, s, force_transfer);
    cairo_surface_flush(s);

    return s;
}

RrAppearance *RrAppearanceNew(const RrInstance *inst, gint numtex)
{
  RrAppearance *out;

  out = g_slice_new0(RrAppearance);
  out->inst = inst;
  out->textures = numtex;
  out->surface.bevel_light_adjust = 128;
  out->surface.bevel_dark_adjust = 64;
  if (numtex) out->texture = g_new0(RrTexture, numtex);

  return out;
}

void RrAppearanceRemoveTextures(RrAppearance *a)
{
    g_free(a->texture);
    a->textures = 0;
}

void RrAppearanceAddTextures(RrAppearance *a, gint numtex)
{
    g_assert(a->textures == 0);

    a->textures = numtex;
    if (numtex) a->texture = g_new0(RrTexture, numtex);
}

void RrAppearanceClearTextures(RrAppearance *a)
{
    memset(a->texture, 0, a->textures * sizeof(RrTexture));
}

/* deep copy of orig, means reset ref to 1 on copy
 * and copy each thing memwise. */
RrAppearance *RrAppearanceCopy(RrAppearance *orig)
{
    RrSurface *spo, *spc;
    RrAppearance *copy = g_slice_new(RrAppearance);

    copy->inst = orig->inst;

    spo = &(orig->surface);
    spc = &(copy->surface);
    spc->grad = spo->grad;
    spc->relief = spo->relief;
    spc->bevel = spo->bevel;
    if (spo->primary != NULL)
        spc->primary = RrColorNew(copy->inst,
                                  spo->primary->r,
                                  spo->primary->g,
                                  spo->primary->b);
    else spc->primary = NULL;

    if (spo->secondary != NULL)
        spc->secondary = RrColorNew(copy->inst,
                                    spo->secondary->r,
                                    spo->secondary->g,
                                    spo->secondary->b);
    else spc->secondary = NULL;

    if (spo->border_color != NULL)
        spc->border_color = RrColorNew(copy->inst,
                                       spo->border_color->r,
                                       spo->border_color->g,
                                       spo->border_color->b);
    else spc->border_color = NULL;

    if (spo->interlace_color != NULL)
        spc->interlace_color = RrColorNew(copy->inst,
                                       spo->interlace_color->r,
                                       spo->interlace_color->g,
                                       spo->interlace_color->b);
    else spc->interlace_color = NULL;

    if (spo->bevel_dark != NULL)
        spc->bevel_dark = RrColorNew(copy->inst,
                                     spo->bevel_dark->r,
                                     spo->bevel_dark->g,
                                     spo->bevel_dark->b);
    else spc->bevel_dark = NULL;

    if (spo->bevel_light != NULL)
        spc->bevel_light = RrColorNew(copy->inst,
                                      spo->bevel_light->r,
                                      spo->bevel_light->g,
                                      spo->bevel_light->b);
    else spc->bevel_light = NULL;

    if (spo->split_primary != NULL)
        spc->split_primary = RrColorNew(copy->inst,
                                        spo->split_primary->r,
                                        spo->split_primary->g,
                                        spo->split_primary->b);
    else spc->split_primary = NULL;

    if (spo->split_secondary != NULL)
        spc->split_secondary = RrColorNew(copy->inst,
                                        spo->split_secondary->r,
                                        spo->split_secondary->g,
                                        spo->split_secondary->b);
    else spc->split_secondary = NULL;

    spc->interlaced = spo->interlaced;
    spc->bevel_light_adjust = spo->bevel_light_adjust;
    spc->bevel_dark_adjust = spo->bevel_dark_adjust;
    spc->border = spo->border;
    spc->parent = NULL;
    spc->parentx = spc->parenty = 0;
    spc->pixel_data = NULL;

    copy->textures = orig->textures;
    copy->texture = g_memdup2(orig->texture,
                             orig->textures * sizeof(RrTexture));
    copy->w = copy->h = 0;
    return copy;
}

/* now decrements ref counter, and frees only if ref <= 0 */
void RrAppearanceFree(RrAppearance *a)
{
    if (a) {
        RrSurface *p;
        if (a->textures)
            g_free(a->texture);
        p = &a->surface;
        RrColorFree(p->primary);
        RrColorFree(p->secondary);
        RrColorFree(p->border_color);
        RrColorFree(p->interlace_color);
        RrColorFree(p->bevel_dark);
        RrColorFree(p->bevel_light);
        RrColorFree(p->split_primary);
        RrColorFree(p->split_secondary);
        g_free(p->pixel_data);
        p->pixel_data = NULL;
        g_slice_free(RrAppearance, a);
    }
}

void RrMargins (RrAppearance *a, gint *l, gint *t, gint *r, gint *b)
{
    *l = *t = *r = *b = 0;

    if (a->surface.grad != RR_SURFACE_PARENTREL) {
        if (a->surface.relief != RR_RELIEF_FLAT) {
            switch (a->surface.bevel) {
            case RR_BEVEL_1:
                *l = *t = *r = *b = 1;
                break;
            case RR_BEVEL_2:
                *l = *t = *r = *b = 2;
                break;
            case RR_BEVEL_NUM_TYPES:
                g_assert_not_reached();
            }
        } else if (a->surface.border) {
            *l = *t = *r = *b = 1;
        }
    }
    /* owbox: the lines are wider at a scale */
    *l *= RrLineWidth(a->inst);
    *t *= RrLineWidth(a->inst);
    *r *= RrLineWidth(a->inst);
    *b *= RrLineWidth(a->inst);
}

void RrMinSize(RrAppearance *a, gint *w, gint *h)
{
    *w = RrMinWidth(a);
    *h = RrMinHeight(a);
}

gint RrMinWidth(RrAppearance *a)
{
    gint i;
    RrSize *m;
    gint l, t, r, b;
    gint w = 0;

    RrMargins(a, &l, &t, &r, &b);

    for (i = 0; i < a->textures; ++i) {
        switch (a->texture[i].type) {
        case RR_TEXTURE_NONE:
            break;
        case RR_TEXTURE_MASK:
            w = MAX(w, a->texture[i].data.mask.mask->width);
            break;
        case RR_TEXTURE_TEXT:
            m = RrFontMeasureString(a->texture[i].data.text.font,
                                    a->texture[i].data.text.string,
                                    a->texture[i].data.text.shadow_offset_x,
                                    a->texture[i].data.text.shadow_offset_y,
                                    a->texture[i].data.text.flow,
                                    a->texture[i].data.text.maxwidth);
            w = MAX(w, m->width);
            g_slice_free(RrSize, m);
            break;
        case RR_TEXTURE_RGBA:
            w += MAX(w, a->texture[i].data.rgba.width);
            break;
        case RR_TEXTURE_IMAGE:
            /* images resize so they don't contribute anything to the min */
            break;
        case RR_TEXTURE_LINE_ART:
            w = MAX(w, MAX(a->texture[i].data.lineart.x1 - l - r,
                           a->texture[i].data.lineart.x2 - l - r));
            break;
        case RR_TEXTURE_NUM_TYPES:
            g_assert_not_reached();
        }
    }

    w += l + r;

    if (w < 1) w = 1;
    return w;
}

gint RrMinHeight(RrAppearance *a)
{
    gint i;
    gint l, t, r, b;
    RrSize *m;
    gint h = 0;

    RrMargins(a, &l, &t, &r, &b);

    for (i = 0; i < a->textures; ++i) {
        switch (a->texture[i].type) {
        case RR_TEXTURE_NONE:
            break;
        case RR_TEXTURE_MASK:
            h = MAX(h, a->texture[i].data.mask.mask->height);
            break;
        case RR_TEXTURE_TEXT:
            if (a->texture[i].data.text.flow) {
                g_assert(a->texture[i].data.text.string != NULL);

                m = RrFontMeasureString
                    (a->texture[i].data.text.font,
                     a->texture[i].data.text.string,
                     a->texture[i].data.text.shadow_offset_x,
                     a->texture[i].data.text.shadow_offset_y,
                     a->texture[i].data.text.flow,
                     a->texture[i].data.text.maxwidth);
                h += MAX(h, m->height);
                g_slice_free(RrSize, m);
            }
            else
                h += MAX(h,
                         RrFontHeight
                         (a->texture[i].data.text.font,
                          a->texture[i].data.text.shadow_offset_y));
            break;
        case RR_TEXTURE_RGBA:
            h += MAX(h, a->texture[i].data.rgba.height);
            break;
        case RR_TEXTURE_IMAGE:
            /* images resize so they don't contribute anything to the min */
            break;
        case RR_TEXTURE_LINE_ART:
            h = MAX(h, MAX(a->texture[i].data.lineart.y1 - t - b,
                           a->texture[i].data.lineart.y2 - t - b));
            break;
        case RR_TEXTURE_NUM_TYPES:
            g_assert_not_reached();
        }
    }

    h += t + b;

    if (h < 1) h = 1;
    return h;
}
