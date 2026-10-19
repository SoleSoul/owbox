/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   grab.c for owbox (ported from the Openbox window manager)
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

#include "grab.h"
#include "seat.h"

static guint kgrabs = 0;
static guint pgrabs = 0;
static ObCursor pgrab_cursor = OB_CURSOR_NONE;

void grab_startup(gboolean reconfig)
{
    if (reconfig) return;
    kgrabs = pgrabs = 0;
}

void grab_shutdown(gboolean reconfig)
{
    if (reconfig) return;

    while (ungrab_keyboard());
    while (ungrab_pointer());
}

gboolean grab_keyboard_full(gboolean grab)
{
    if (grab) {
        if (kgrabs++ == 0)
            seat_keyboard_grabbed(TRUE);
        return TRUE;
    } else if (kgrabs > 0) {
        if (--kgrabs == 0)
            seat_keyboard_grabbed(FALSE);
        return TRUE;
    }
    return FALSE;
}

gboolean grab_pointer_full(gboolean grab, gboolean owner_events,
                           gboolean confine, ObCursor cur)
{
    if (grab) {
        if (pgrabs++ == 0) {
            pgrab_cursor = cur;
            seat_pointer_grabbed(TRUE);
        }
        return TRUE;
    } else if (pgrabs > 0) {
        if (--pgrabs == 0) {
            pgrab_cursor = OB_CURSOR_NONE;
            seat_pointer_grabbed(FALSE);
        }
        return TRUE;
    }
    return FALSE;
}

gboolean grab_on_keyboard(void)
{
    return kgrabs > 0;
}

gboolean grab_on_pointer(void)
{
    return pgrabs > 0;
}

ObCursor grab_pointer_cursor(void)
{
    return pgrab_cursor;
}

void grab_set_pointer_cursor(ObCursor cur)
{
    if (pgrabs) {
        pgrab_cursor = cur;
        seat_update_cursor();
    }
}

void ungrab_passive_key(void)
{
}
