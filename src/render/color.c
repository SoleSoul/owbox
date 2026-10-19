/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   color.c for owbox (ported from the Openbox window manager)
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
#include "color.h"
#include "instance.h"
#include "colornames.h"

#include <stdlib.h>
#include <string.h>

static gboolean parse_hex(const gchar *s, gint len, gint *v)
{
    gint i, r = 0;
    for (i = 0; i < len; ++i) {
        if (!g_ascii_isxdigit(s[i])) return FALSE;
        r = r * 16 + g_ascii_xdigit_value(s[i]);
    }
    /* scale an n-digit value to 8 bits, the way Xlib does */
    switch (len) {
    case 1: *v = r * 0x11; break;
    case 2: *v = r; break;
    case 3: *v = r >> 4; break;
    case 4: *v = r >> 8; break;
    default: return FALSE;
    }
    return TRUE;
}

static int cmp_name(const void *key, const void *elem)
{
    return strcmp(key, ((const RrColorName*)elem)->name);
}

/*! Parses the same color specifications as XParseColor: #rgb, #rrggbb,
  #rrrgggbbb, #rrrrggggbbbb, rgb:r/g/b and the names from X's rgb.txt */
static gboolean parse_color(const gchar *spec, gint *r, gint *g, gint *b)
{
    gsize len = strlen(spec);

    if (spec[0] == '#') {
        gint n = (len - 1) / 3;
        if (len < 4 || (len - 1) % 3 || n > 4) return FALSE;
        return parse_hex(spec + 1, n, r) &&
            parse_hex(spec + 1 + n, n, g) &&
            parse_hex(spec + 1 + 2*n, n, b);
    } else if (!g_ascii_strncasecmp(spec, "rgb:", 4)) {
        gchar **parts = g_strsplit(spec + 4, "/", -1);
        gboolean ok = g_strv_length(parts) == 3 &&
            parse_hex(parts[0], strlen(parts[0]), r) &&
            parse_hex(parts[1], strlen(parts[1]), g) &&
            parse_hex(parts[2], strlen(parts[2]), b);
        g_strfreev(parts);
        return ok;
    } else {
        GString *key = g_string_new(NULL);
        const RrColorName *c;
        const gchar *p;
        for (p = spec; *p; ++p)
            if (*p != ' ') g_string_append_c(key, g_ascii_tolower(*p));
        c = bsearch(key->str, rr_color_names, G_N_ELEMENTS(rr_color_names),
                    sizeof(RrColorName), cmp_name);
        g_string_free(key, TRUE);
        if (!c) return FALSE;
        *r = c->r; *g = c->g; *b = c->b;
        return TRUE;
    }
}

RrColor *RrColorParse(const RrInstance *inst, gchar *colorname)
{
    gint r, g, b;

    g_assert(colorname != NULL);
    /* get rgb values from colorname */

    if (!parse_color(colorname, &r, &g, &b)) {
        g_message("Unable to parse color '%s'", colorname);
        return NULL;
    }
    return RrColorNew(inst, r, g, b);
}

RrColor *RrColorNew(const RrInstance *inst, gint r, gint g, gint b)
{
    RrColor *out = NULL;
    gint key;

    g_assert(r >= 0 && r < 256);
    g_assert(g >= 0 && g < 256);
    g_assert(b >= 0 && b < 256);

    key = (r << 24) + (g << 16) + (b << 8);
    if ((out = g_hash_table_lookup(RrColorHash(inst), &key))) {
        out->refcount++;
    } else {
        out = g_slice_new(RrColor);
        out->inst = inst;
        out->r = r;
        out->g = g;
        out->b = b;
        out->key = key;
        out->refcount = 1;
        g_hash_table_insert(RrColorHash(inst), &out->key, out);
    }
    return out;
}

RrColor *RrColorCopy(RrColor* c)
{
    return RrColorNew(c->inst, c->r, c->g, c->b);
}

void RrColorFree(RrColor *c)
{
    if (c) {
        if (--c->refcount < 1) {
            g_assert(g_hash_table_lookup(RrColorHash(c->inst), &c->key));
            g_hash_table_remove(RrColorHash(c->inst), &c->key);
            g_slice_free(RrColor, c);
        }
    }
}

gint RrColorRed(const RrColor *c)
{
    return c->r;
}

gint RrColorGreen(const RrColor *c)
{
    return c->g;
}

gint RrColorBlue(const RrColor *c)
{
    return c->b;
}

RrPixel32 RrColorPixel(const RrColor *c)
{
    return (0xffu << RrDefaultAlphaOffset) |
        (c->r << RrDefaultRedOffset) |
        (c->g << RrDefaultGreenOffset) |
        (c->b << RrDefaultBlueOffset);
}
