/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   focus_cycle_indicator.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2006        Mikael Magnusson
   Copyright (c) 2003-2007   Dana Jansens

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

#include "focus_cycle.h"
#include "focus_cycle_indicator.h"
#include "client.h"
#include "openbox.h"
#include "frame.h"
#include "event.h"
#include "render/render.h"
#include "server.h"
#include "fwin.h"

#include <glib.h>

#define FOCUS_INDICATOR_WIDTH 6

static struct
{
    ObInternalWindow top;
    ObInternalWindow left;
    ObInternalWindow right;
    ObInternalWindow bottom;
} focus_indicator;

static RrAppearance *a_focus_indicator;
static RrColor      *color_white;
static gboolean      visible;

static ObFWin* create_window(ObInternalWindow *w)
{
    return fwin_new(NULL, server.stack_tree[OB_STACKING_LAYER_INTERNAL],
                    OB_NODE_WINDOW, INTERNAL_AS_WINDOW(w));
}

void focus_cycle_indicator_startup(gboolean reconfig)
{
    visible = FALSE;

    if (reconfig) return;

    focus_indicator.top.type = OB_WINDOW_CLASS_INTERNAL;
    focus_indicator.left.type = OB_WINDOW_CLASS_INTERNAL;
    focus_indicator.right.type = OB_WINDOW_CLASS_INTERNAL;
    focus_indicator.bottom.type = OB_WINDOW_CLASS_INTERNAL;

    focus_indicator.top.window = create_window(&focus_indicator.top);
    focus_indicator.left.window = create_window(&focus_indicator.left);
    focus_indicator.right.window = create_window(&focus_indicator.right);
    focus_indicator.bottom.window = create_window(&focus_indicator.bottom);

    stacking_add(INTERNAL_AS_WINDOW(&focus_indicator.top));
    stacking_add(INTERNAL_AS_WINDOW(&focus_indicator.left));
    stacking_add(INTERNAL_AS_WINDOW(&focus_indicator.right));
    stacking_add(INTERNAL_AS_WINDOW(&focus_indicator.bottom));

    color_white = RrColorNew(ob_rr_inst, 0xff, 0xff, 0xff);

    a_focus_indicator = RrAppearanceNew(ob_rr_inst, 4);
    a_focus_indicator->surface.grad = RR_SURFACE_SOLID;
    a_focus_indicator->surface.relief = RR_RELIEF_FLAT;
    a_focus_indicator->surface.primary = RrColorNew(ob_rr_inst,
                                                    0, 0, 0);
    a_focus_indicator->texture[0].type = RR_TEXTURE_LINE_ART;
    a_focus_indicator->texture[0].data.lineart.color = color_white;
    a_focus_indicator->texture[1].type = RR_TEXTURE_LINE_ART;
    a_focus_indicator->texture[1].data.lineart.color = color_white;
    a_focus_indicator->texture[2].type = RR_TEXTURE_LINE_ART;
    a_focus_indicator->texture[2].data.lineart.color = color_white;
    a_focus_indicator->texture[3].type = RR_TEXTURE_LINE_ART;
    a_focus_indicator->texture[3].data.lineart.color = color_white;
}

void focus_cycle_indicator_shutdown(gboolean reconfig)
{
    if (reconfig) return;

    RrColorFree(color_white);

    RrAppearanceFree(a_focus_indicator);


    stacking_remove(INTERNAL_AS_WINDOW(&focus_indicator.top));
    stacking_remove(INTERNAL_AS_WINDOW(&focus_indicator.left));
    stacking_remove(INTERNAL_AS_WINDOW(&focus_indicator.right));
    stacking_remove(INTERNAL_AS_WINDOW(&focus_indicator.bottom));

    fwin_destroy(focus_indicator.top.window);
    fwin_destroy(focus_indicator.left.window);
    fwin_destroy(focus_indicator.right.window);
    fwin_destroy(focus_indicator.bottom.window);
}

void focus_cycle_update_indicator(ObClient *c)
{
        if (visible)
            focus_cycle_draw_indicator(c);
}

void focus_cycle_draw_indicator(ObClient *c)
{
    if (!c && visible) {
        gulong ignore_start;

        /* kill enter events cause by this unmapping */
        ignore_start = event_start_ignore_all_enters();

        fwin_unmap(focus_indicator.top.window);
        fwin_unmap(focus_indicator.left.window);
        fwin_unmap(focus_indicator.right.window);
        fwin_unmap(focus_indicator.bottom.window);

        event_end_ignore_all_enters(ignore_start);

        visible = FALSE;
    }
    else if (c) {
        /*
          if (c)
              frame_adjust_focus(c->frame, FALSE);
          frame_adjust_focus(c->frame, TRUE);
        */
        gint x, y, w, h;
        gint wt, wl, wr, wb;
        gulong ignore_start;

        wt = wl = wr = wb = FOCUS_INDICATOR_WIDTH;

        x = c->frame->area.x;
        y = c->frame->area.y;
        w = c->frame->area.width;
        h = wt;

        /* kill enter events cause by this moving */
        ignore_start = event_start_ignore_all_enters();

        fwin_move_resize(focus_indicator.top.window,
                          x, y, w, h);
        a_focus_indicator->texture[0].data.lineart.x1 = 0;
        a_focus_indicator->texture[0].data.lineart.y1 = h-1;
        a_focus_indicator->texture[0].data.lineart.x2 = 0;
        a_focus_indicator->texture[0].data.lineart.y2 = 0;
        a_focus_indicator->texture[1].data.lineart.x1 = 0;
        a_focus_indicator->texture[1].data.lineart.y1 = 0;
        a_focus_indicator->texture[1].data.lineart.x2 = w-1;
        a_focus_indicator->texture[1].data.lineart.y2 = 0;
        a_focus_indicator->texture[2].data.lineart.x1 = w-1;
        a_focus_indicator->texture[2].data.lineart.y1 = 0;
        a_focus_indicator->texture[2].data.lineart.x2 = w-1;
        a_focus_indicator->texture[2].data.lineart.y2 = h-1;
        a_focus_indicator->texture[3].data.lineart.x1 = (wl-1);
        a_focus_indicator->texture[3].data.lineart.y1 = h-1;
        a_focus_indicator->texture[3].data.lineart.x2 = w - wr;
        a_focus_indicator->texture[3].data.lineart.y2 = h-1;
        fwin_paint(focus_indicator.top.window, a_focus_indicator,
                w, h);

        x = c->frame->area.x;
        y = c->frame->area.y;
        w = wl;
        h = c->frame->area.height;

        fwin_move_resize(focus_indicator.left.window,
                          x, y, w, h);
        a_focus_indicator->texture[0].data.lineart.x1 = w-1;
        a_focus_indicator->texture[0].data.lineart.y1 = 0;
        a_focus_indicator->texture[0].data.lineart.x2 = 0;
        a_focus_indicator->texture[0].data.lineart.y2 = 0;
        a_focus_indicator->texture[1].data.lineart.x1 = 0;
        a_focus_indicator->texture[1].data.lineart.y1 = 0;
        a_focus_indicator->texture[1].data.lineart.x2 = 0;
        a_focus_indicator->texture[1].data.lineart.y2 = h-1;
        a_focus_indicator->texture[2].data.lineart.x1 = 0;
        a_focus_indicator->texture[2].data.lineart.y1 = h-1;
        a_focus_indicator->texture[2].data.lineart.x2 = w-1;
        a_focus_indicator->texture[2].data.lineart.y2 = h-1;
        a_focus_indicator->texture[3].data.lineart.x1 = w-1;
        a_focus_indicator->texture[3].data.lineart.y1 = wt-1;
        a_focus_indicator->texture[3].data.lineart.x2 = w-1;
        a_focus_indicator->texture[3].data.lineart.y2 = h - wb;
        fwin_paint(focus_indicator.left.window, a_focus_indicator,
                w, h);

        x = c->frame->area.x + c->frame->area.width - wr;
        y = c->frame->area.y;
        w = wr;
        h = c->frame->area.height ;

        fwin_move_resize(focus_indicator.right.window,
                          x, y, w, h);
        a_focus_indicator->texture[0].data.lineart.x1 = 0;
        a_focus_indicator->texture[0].data.lineart.y1 = 0;
        a_focus_indicator->texture[0].data.lineart.x2 = w-1;
        a_focus_indicator->texture[0].data.lineart.y2 = 0;
        a_focus_indicator->texture[1].data.lineart.x1 = w-1;
        a_focus_indicator->texture[1].data.lineart.y1 = 0;
        a_focus_indicator->texture[1].data.lineart.x2 = w-1;
        a_focus_indicator->texture[1].data.lineart.y2 = h-1;
        a_focus_indicator->texture[2].data.lineart.x1 = w-1;
        a_focus_indicator->texture[2].data.lineart.y1 = h-1;
        a_focus_indicator->texture[2].data.lineart.x2 = 0;
        a_focus_indicator->texture[2].data.lineart.y2 = h-1;
        a_focus_indicator->texture[3].data.lineart.x1 = 0;
        a_focus_indicator->texture[3].data.lineart.y1 = wt-1;
        a_focus_indicator->texture[3].data.lineart.x2 = 0;
        a_focus_indicator->texture[3].data.lineart.y2 = h - wb;
        fwin_paint(focus_indicator.right.window, a_focus_indicator,
                w, h);

        x = c->frame->area.x;
        y = c->frame->area.y + c->frame->area.height - wb;
        w = c->frame->area.width;
        h = wb;

        fwin_move_resize(focus_indicator.bottom.window,
                          x, y, w, h);
        a_focus_indicator->texture[0].data.lineart.x1 = 0;
        a_focus_indicator->texture[0].data.lineart.y1 = 0;
        a_focus_indicator->texture[0].data.lineart.x2 = 0;
        a_focus_indicator->texture[0].data.lineart.y2 = h-1;
        a_focus_indicator->texture[1].data.lineart.x1 = 0;
        a_focus_indicator->texture[1].data.lineart.y1 = h-1;
        a_focus_indicator->texture[1].data.lineart.x2 = w-1;
        a_focus_indicator->texture[1].data.lineart.y2 = h-1;
        a_focus_indicator->texture[2].data.lineart.x1 = w-1;
        a_focus_indicator->texture[2].data.lineart.y1 = h-1;
        a_focus_indicator->texture[2].data.lineart.x2 = w-1;
        a_focus_indicator->texture[2].data.lineart.y2 = 0;
        a_focus_indicator->texture[3].data.lineart.x1 = wl-1;
        a_focus_indicator->texture[3].data.lineart.y1 = 0;
        a_focus_indicator->texture[3].data.lineart.x2 = w - wr;
        a_focus_indicator->texture[3].data.lineart.y2 = 0;
        fwin_paint(focus_indicator.bottom.window, a_focus_indicator,
                w, h);

        fwin_map(focus_indicator.top.window);
        fwin_map(focus_indicator.left.window);
        fwin_map(focus_indicator.right.window);
        fwin_map(focus_indicator.bottom.window);

        event_end_ignore_all_enters(ignore_start);

        visible = TRUE;
    }
}
