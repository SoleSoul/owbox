/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   screen.c for owbox (ported from the Openbox window manager)
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

#include "debug.h"
#include "openbox.h"
#include "grab.h"
#include "moveresize.h"
#include "config.h"
#include "screen.h"
#include "client.h"
#include "frame.h"
#include "event.h"
#include "focus.h"
#include "focus_cycle.h"
#include "popup.h"
#include "output.h"
#include "layers.h"
#include "seat.h"
#include "toplevel.h"
#include "xwayland.h"
#include "render/render.h"
#include "gravity.h"
#include "gettext.h"

#ifdef HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif
#include <assert.h>

static gboolean screen_validate_layout(ObDesktopLayout *l);
static void     screen_fallback_focus(void);

guint                  screen_num_desktops;
guint                  screen_num_monitors;
guint                  screen_desktop;
guint                  screen_last_desktop;
ObScreenShowDestopMode screen_show_desktop_mode;
ObDesktopLayout        screen_desktop_layout;
gchar                **screen_desktop_names;
guint32                screen_desktop_user_time = 0;

static Size     screen_physical_size;
static guint    screen_old_desktop;
static gboolean screen_desktop_timeout = TRUE;
static guint    screen_desktop_timer = 0;
/*! An array of desktops, holding an array of areas per monitor */
static Rect  *monitor_area = NULL;
/*! An array of desktops, holding an array of struts */
static GSList *struts_top = NULL;
static GSList *struts_left = NULL;
static GSList *struts_right = NULL;
static GSList *struts_bottom = NULL;

static ObPagerPopup **desktop_popup;
static guint         desktop_popup_timer = 0;
static gboolean      desktop_popup_perm;

/*! The number of microseconds that you need to be on a desktop before it will
  replace the remembered "last desktop" */
#define REMEMBER_LAST_DESKTOP_TIME 750

static void desktop_popup_new()
{
        guint i;
        desktop_popup = g_new(ObPagerPopup*, screen_num_monitors);
        for (i = 0; i < screen_num_monitors; i++) {
            desktop_popup[i] = pager_popup_new();
            desktop_popup[i]->popup->a_text->texture[0].data.text.font = ob_rr_theme->menu_title_font;
            pager_popup_height(desktop_popup[i], POPUP_HEIGHT);

            /* update the pager popup's width */
            if (screen_desktop_names)
                pager_popup_text_width_to_strings(desktop_popup[i],
                                                  screen_desktop_names,
                                                  screen_num_desktops);
        }

}

void screen_startup(gboolean reconfig)
{
    desktop_popup_perm = FALSE;

    if (reconfig) {
        desktop_popup_new();
        screen_update_layout();
        /* the number of desktops or their names can change in the config */
        screen_set_num_desktops(config_desktops_num);
        screen_update_desktop_names();
        return;
    } else {
        if (desktop_popup)
            ob_debug("desktop_popup wasn't NULL when expected %p",
                     desktop_popup);
        desktop_popup = NULL;
    }

    /* get the initial size */
    screen_resize();

    /* set the number of desktops.

       this will also set the default names from the config file up for
       desktops that don't have names yet */
    screen_num_desktops = 0;
    screen_set_num_desktops(config_desktops_num);

    screen_desktop = screen_num_desktops;  /* something invalid */
    screen_set_desktop(MIN(config_screen_firstdesk,
                           screen_num_desktops) - 1, FALSE);
    screen_last_desktop = screen_desktop;

    /* don't start in showing-desktop mode */
    screen_show_desktop_mode = SCREEN_SHOW_DESKTOP_NO;

    screen_update_layout();
}

static void desktop_popup_free(guint n)
{
    guint i;
    for (i = 0; i < n; i++) {
        pager_popup_free(desktop_popup[i]);
    }
    g_free(desktop_popup);
    desktop_popup = NULL;
}

void screen_shutdown(gboolean reconfig)
{
    desktop_popup_free(screen_num_monitors);

    if (reconfig)
        return;

    g_strfreev(screen_desktop_names);
    screen_desktop_names = NULL;
}

void screen_resize(void)
{
    GList *it;
    const Rect *all;

    /* the outputs changed, find the new monitors */
    screen_update_areas();

    all = screen_physical_area_all_monitors();
    screen_physical_size.width = all->width;
    screen_physical_size.height = all->height;

    if (ob_state() != OB_STATE_RUNNING)
        return;

    for (it = client_list; it; it = g_list_next(it)) {
        client_move_onscreen(it->data, FALSE);
        client_reconfigure(it->data, FALSE);
    }
}

void screen_set_num_desktops(guint num)
{
    GList *it, *stacking_copy;

    g_assert(num > 0);

    if (screen_num_desktops == num) return;

    screen_num_desktops = num;

    /* the number of rows/columns will differ */
    screen_update_layout();

    /* move windows on desktops that will no longer exist!
       make a copy of the list cuz we're changing it */
    stacking_copy = g_list_copy(stacking_list);
    for (it = g_list_last(stacking_copy); it; it = g_list_previous(it)) {
        if (WINDOW_IS_CLIENT(it->data)) {
            ObClient *c = it->data;
            if (c->desktop != DESKTOP_ALL && c->desktop >= num)
                client_set_desktop(c, num - 1, FALSE, TRUE);
            /* raise all the windows that are on the current desktop which
               is being merged */
            else if (screen_desktop == num - 1 &&
                     (c->desktop == DESKTOP_ALL ||
                      c->desktop == screen_desktop))
                stacking_raise(CLIENT_AS_WINDOW(c));
        }
    }
    g_list_free(stacking_copy);

    /* change our struts/area to match (after moving windows) */
    screen_update_areas();

    /* may be some unnamed desktops that we need to fill in with names
       (after updating the areas so the popup can resize) */
    screen_update_desktop_names();

    /* change our desktop if we're on one that no longer exists! */
    if (screen_desktop >= screen_num_desktops && screen_desktop != num)
        screen_set_desktop(num - 1, TRUE);

    toplevel_update_desktops();
}

static void screen_fallback_focus(void)
{
    ObClient *c;
    gboolean allow_omni;

    /* only allow omnipresent windows to get focus on desktop change if
       an omnipresent window is already focused (it'll keep focus probably, but
       maybe not depending on mouse-focus options) */
    allow_omni = focus_client && (client_normal(focus_client) &&
                                  focus_client->desktop == DESKTOP_ALL);

    /* the client moved there already so don't move focus. prevent flicker
       on sendtodesktop + follow */
    if (focus_client && focus_client->desktop == screen_desktop)
        return;

    /* have to try focus here because when you leave an empty desktop
       there is no focus out to watch for. also, we have different rules
       here. we always allow it to look under the mouse pointer if
       config_focus_last is FALSE

       do this before hiding the windows so if helper windows are coming
       with us, they don't get hidden
    */
    if ((c = focus_fallback(TRUE, !config_focus_last, allow_omni,
                            !allow_omni)))
    {
        /* only do the flicker reducing stuff ahead of time if we are going
           to call xsetinputfocus on the window ourselves. otherwise there is
           no guarantee the window will actually take focus.. */
        if (c->can_focus) {
            /* reduce flicker by hiliting now rather than waiting for the
               server FocusIn event */
            frame_adjust_focus(c->frame, TRUE);
            /* do this here so that if you switch desktops to a window with
               helper windows then the helper windows won't flash */
            client_bring_helper_windows(c);
        }
    }
}

static gboolean last_desktop_func(gpointer data)
{
    screen_desktop_timeout = TRUE;
    screen_desktop_timer = 0;
    return FALSE; /* don't repeat */
}

void screen_set_desktop(guint num, gboolean dofocus)
{
    GList *it;
    guint previous;
    gulong ignore_start;

    g_assert(num < screen_num_desktops);

    previous = screen_desktop;
    screen_desktop = num;

    if (ob_state() == OB_STATE_RUNNING)
        screen_show_desktop_popup(screen_desktop, FALSE);

    if (previous == num) return;

    toplevel_update_desktops();

    /* This whole thing decides when/how to save the screen_last_desktop so
       that it can be restored later if you want */
    if (screen_desktop_timeout) {
        /* If screen_desktop_timeout is true, then we've been on this desktop
           long enough and we can save it as the last desktop. */

        if (screen_last_desktop == previous)
            /* this is the startup state only */
            screen_old_desktop = screen_desktop;
        else {
            /* save the "last desktop" as the "old desktop" */
            screen_old_desktop = screen_last_desktop;
            /* save the desktop we're coming from as the "last desktop" */
            screen_last_desktop = previous;
        }
    }
    else {
        /* If screen_desktop_timeout is false, then we just got to this desktop
           and we are moving away again. */

        if (screen_desktop == screen_last_desktop) {
            /* If we are moving to the "last desktop" .. */
            if (previous == screen_old_desktop) {
                /* .. from the "old desktop", change the last desktop to
                   be where we are coming from */
                screen_last_desktop = screen_old_desktop;
            }
            else if (screen_last_desktop == screen_old_desktop) {
                /* .. and also to the "old desktop", change the "last
                   desktop" to be where we are coming from */
                screen_last_desktop = previous;
            }
            else {
                /* .. from some other desktop, then set the "last desktop" to
                   be the saved "old desktop", i.e. where we were before the
                   "last desktop" */
                screen_last_desktop = screen_old_desktop;
            }
        }
        else {
            /* If we are moving to any desktop besides the "last desktop"..
               (this is the normal case) */
            if (screen_desktop == screen_old_desktop) {
                /* If moving to the "old desktop", which is not the
                   "last desktop", don't save anything */
            }
            else if (previous == screen_old_desktop) {
                /* If moving from the "old desktop", and not to the
                   "last desktop", don't save anything */
            }
            else if (screen_last_desktop == screen_old_desktop) {
                /* If the "last desktop" is the same as "old desktop" and
                   you're not moving to the "last desktop" then save where
                   we're coming from as the "last desktop" */
                screen_last_desktop = previous;
            }
            else {
                /* If the "last desktop" is different from the "old desktop"
                   and you're not moving to the "last desktop", then don't save
                   anything */
            }
        }
    }
    screen_desktop_timeout = FALSE;
    if (screen_desktop_timer) g_source_remove(screen_desktop_timer);
    screen_desktop_timer = g_timeout_add(REMEMBER_LAST_DESKTOP_TIME,
                                         last_desktop_func, NULL);

    ob_debug("Moving to desktop %d", num+1);

    /* ignore enter events caused by the move */
    ignore_start = event_start_ignore_all_enters();

    if (moveresize_client)
        client_set_desktop(moveresize_client, num, TRUE, FALSE);

    /* show windows before hiding the rest to lessen the enter/leave events */

    /* show windows from top to bottom */
    for (it = stacking_list; it; it = g_list_next(it)) {
        if (WINDOW_IS_CLIENT(it->data)) {
            ObClient *c = it->data;
            client_show(c);
        }
    }

    if (dofocus) screen_fallback_focus();

    /* hide windows from bottom to top */
    for (it = g_list_last(stacking_list); it; it = g_list_previous(it)) {
        if (WINDOW_IS_CLIENT(it->data)) {
            ObClient *c = it->data;
            if (client_hide(c)) {
                if (c == focus_client) {
                    /* c was focused and we didn't do fallback clearly so make
                       sure openbox doesnt still consider the window focused.
                       this happens when using NextWindow with allDesktops,
                       since it doesnt want to move focus on desktop change,
                       but the focus is not going to stay with the current
                       window, which has now disappeared.
                       only do this if the client was actually hidden,
                       otherwise it can keep focus. */
                    focus_set_client(NULL);
                }
            }
        }
    }

    focus_cycle_addremove(NULL, TRUE);

    event_end_ignore_all_enters(ignore_start);

    if (event_source_time() != 0)
        screen_desktop_user_time = event_source_time();
}

void screen_add_desktop(gboolean current)
{
    gulong ignore_start;

    /* ignore enter events caused by this */
    ignore_start = event_start_ignore_all_enters();

    screen_set_num_desktops(screen_num_desktops+1);

    /* move all the clients over */
    if (current) {
        GList *it;

        for (it = client_list; it; it = g_list_next(it)) {
            ObClient *c = it->data;
            if (c->desktop != DESKTOP_ALL && c->desktop >= screen_desktop &&
                /* don't move direct children, they'll be moved with their
                   parent - which will have to be on the same desktop */
                !client_direct_parent(c))
            {
                ob_debug("moving window %s", c->title);
                client_set_desktop(c, c->desktop+1, FALSE, TRUE);
            }
        }
    }

    event_end_ignore_all_enters(ignore_start);
}

void screen_remove_desktop(gboolean current)
{
    guint rmdesktop, movedesktop;
    GList *it, *stacking_copy;
    gulong ignore_start;

    if (screen_num_desktops <= 1) return;

    /* ignore enter events caused by this */
    ignore_start = event_start_ignore_all_enters();

    /* what desktop are we removing and moving to? */
    if (current)
        rmdesktop = screen_desktop;
    else
        rmdesktop = screen_num_desktops - 1;
    if (rmdesktop < screen_num_desktops - 1)
        movedesktop = rmdesktop + 1;
    else
        movedesktop = rmdesktop;

    /* make a copy of the list cuz we're changing it */
    stacking_copy = g_list_copy(stacking_list);
    for (it = g_list_last(stacking_copy); it; it = g_list_previous(it)) {
        if (WINDOW_IS_CLIENT(it->data)) {
            ObClient *c = it->data;
            guint d = c->desktop;
            if (d != DESKTOP_ALL && d >= movedesktop &&
                /* don't move direct children, they'll be moved with their
                   parent - which will have to be on the same desktop */
                !client_direct_parent(c))
            {
                ob_debug("moving window %s", c->title);
                client_set_desktop(c, c->desktop - 1, TRUE, TRUE);
            }
            /* raise all the windows that are on the current desktop which
               is being merged */
            if ((screen_desktop == rmdesktop - 1 ||
                 screen_desktop == rmdesktop) &&
                (d == DESKTOP_ALL || d == screen_desktop))
            {
                stacking_raise(CLIENT_AS_WINDOW(c));
                ob_debug("raising window %s", c->title);
            }
        }
    }
    g_list_free(stacking_copy);

    /* fallback focus like we're changing desktops */
    if (screen_desktop < screen_num_desktops - 1) {
        screen_fallback_focus();
        ob_debug("fake desktop change");
    }

    screen_set_num_desktops(screen_num_desktops-1);

    event_end_ignore_all_enters(ignore_start);
}

static void get_row_col(guint d, guint *r, guint *c)
{
    switch (screen_desktop_layout.orientation) {
    case OB_ORIENTATION_HORZ:
        switch (screen_desktop_layout.start_corner) {
        case OB_CORNER_TOPLEFT:
            *r = d / screen_desktop_layout.columns;
            *c = d % screen_desktop_layout.columns;
            break;
        case OB_CORNER_BOTTOMLEFT:
            *r = screen_desktop_layout.rows - 1 -
                d / screen_desktop_layout.columns;
            *c = d % screen_desktop_layout.columns;
            break;
        case OB_CORNER_TOPRIGHT:
            *r = d / screen_desktop_layout.columns;
            *c = screen_desktop_layout.columns - 1 -
                d % screen_desktop_layout.columns;
            break;
        case OB_CORNER_BOTTOMRIGHT:
            *r = screen_desktop_layout.rows - 1 -
                d / screen_desktop_layout.columns;
            *c = screen_desktop_layout.columns - 1 -
                d % screen_desktop_layout.columns;
            break;
        }
        break;
    case OB_ORIENTATION_VERT:
        switch (screen_desktop_layout.start_corner) {
        case OB_CORNER_TOPLEFT:
            *r = d % screen_desktop_layout.rows;
            *c = d / screen_desktop_layout.rows;
            break;
        case OB_CORNER_BOTTOMLEFT:
            *r = screen_desktop_layout.rows - 1 -
                d % screen_desktop_layout.rows;
            *c = d / screen_desktop_layout.rows;
            break;
        case OB_CORNER_TOPRIGHT:
            *r = d % screen_desktop_layout.rows;
            *c = screen_desktop_layout.columns - 1 -
                d / screen_desktop_layout.rows;
            break;
        case OB_CORNER_BOTTOMRIGHT:
            *r = screen_desktop_layout.rows - 1 -
                d % screen_desktop_layout.rows;
            *c = screen_desktop_layout.columns - 1 -
                d / screen_desktop_layout.rows;
            break;
        }
        break;
    }
}

static guint translate_row_col(guint r, guint c)
{
    switch (screen_desktop_layout.orientation) {
    case OB_ORIENTATION_HORZ:
        switch (screen_desktop_layout.start_corner) {
        case OB_CORNER_TOPLEFT:
            return r % screen_desktop_layout.rows *
                screen_desktop_layout.columns +
                c % screen_desktop_layout.columns;
        case OB_CORNER_BOTTOMLEFT:
            return (screen_desktop_layout.rows - 1 -
                    r % screen_desktop_layout.rows) *
                screen_desktop_layout.columns +
                c % screen_desktop_layout.columns;
        case OB_CORNER_TOPRIGHT:
            return r % screen_desktop_layout.rows *
                screen_desktop_layout.columns +
                (screen_desktop_layout.columns - 1 -
                 c % screen_desktop_layout.columns);
        case OB_CORNER_BOTTOMRIGHT:
            return (screen_desktop_layout.rows - 1 -
                    r % screen_desktop_layout.rows) *
                screen_desktop_layout.columns +
                (screen_desktop_layout.columns - 1 -
                 c % screen_desktop_layout.columns);
        }
    case OB_ORIENTATION_VERT:
        switch (screen_desktop_layout.start_corner) {
        case OB_CORNER_TOPLEFT:
            return c % screen_desktop_layout.columns *
                screen_desktop_layout.rows +
                r % screen_desktop_layout.rows;
        case OB_CORNER_BOTTOMLEFT:
            return c % screen_desktop_layout.columns *
                screen_desktop_layout.rows +
                (screen_desktop_layout.rows - 1 -
                 r % screen_desktop_layout.rows);
        case OB_CORNER_TOPRIGHT:
            return (screen_desktop_layout.columns - 1 -
                    c % screen_desktop_layout.columns) *
                screen_desktop_layout.rows +
                r % screen_desktop_layout.rows;
        case OB_CORNER_BOTTOMRIGHT:
            return (screen_desktop_layout.columns - 1 -
                    c % screen_desktop_layout.columns) *
                screen_desktop_layout.rows +
                (screen_desktop_layout.rows - 1 -
                 r % screen_desktop_layout.rows);
        }
    }
    g_assert_not_reached();
    return 0;
}

static gboolean hide_desktop_popup_func(gpointer data)
{
    guint i;

    desktop_popup_timer = 0;

    for (i = 0; i < screen_num_monitors; i++) {
        pager_popup_hide(desktop_popup[i]);
    }
    return FALSE; /* don't repeat */
}

void screen_show_desktop_popup(guint d, gboolean perm)
{
    const Rect *a;
    guint i;

    /* 0 means don't show the popup */
    if (!config_desktop_popup_time) return;

    for (i = 0; i < screen_num_monitors; i++) {
        a = screen_physical_area_monitor(i);
        pager_popup_position(desktop_popup[i], CenterGravity,
                             a->x + a->width / 2, a->y + a->height / 2);
        pager_popup_icon_size_multiplier(desktop_popup[i],
                                         (screen_desktop_layout.columns /
                                          screen_desktop_layout.rows) / 2,
                                         (screen_desktop_layout.rows/
                                          screen_desktop_layout.columns) / 2);
        pager_popup_max_width(desktop_popup[i],
                              MAX(a->width/3, POPUP_WIDTH));
        pager_popup_show(desktop_popup[i], screen_desktop_names[d], d);
    }
    if (desktop_popup_timer) g_source_remove(desktop_popup_timer);
    desktop_popup_timer = 0;
    if (!perm && !desktop_popup_perm)
        /* only hide if its not already being show permanently */
        desktop_popup_timer = g_timeout_add(config_desktop_popup_time,
                                            hide_desktop_popup_func,
                                            desktop_popup);
    if (perm)
        desktop_popup_perm = TRUE;
}

void screen_hide_desktop_popup(void)
{
    if (desktop_popup_timer) g_source_remove(desktop_popup_timer);
    desktop_popup_timer = 0;
    desktop_popup_perm = FALSE;
    guint i;

    for (i = 0; i < screen_num_monitors; i++) {
        pager_popup_hide(desktop_popup[i]);
    }
}

guint screen_find_desktop(guint from, ObDirection dir,
                          gboolean wrap, gboolean linear)
{
    guint r, c;
    guint d;

    d = from;
    get_row_col(d, &r, &c);
    if (linear) {
        switch (dir) {
        case OB_DIRECTION_EAST:
            if (d < screen_num_desktops - 1)
                ++d;
            else if (wrap)
                d = 0;
            else
                return from;
            break;
        case OB_DIRECTION_WEST:
            if (d > 0)
                --d;
            else if (wrap)
                d = screen_num_desktops - 1;
            else
                return from;
            break;
        default:
            g_assert_not_reached();
            return from;
        }
    } else {
        switch (dir) {
        case OB_DIRECTION_EAST:
            ++c;
            if (c >= screen_desktop_layout.columns) {
                if (wrap)
                    c = 0;
                else
                    return from;
            }
            d = translate_row_col(r, c);
            if (d >= screen_num_desktops) {
                if (wrap)
                    ++c;
                else
                    return from;
            }
            break;
        case OB_DIRECTION_WEST:
            --c;
            if (c >= screen_desktop_layout.columns) {
                if (wrap)
                    c = screen_desktop_layout.columns - 1;
                else
                    return from;
            }
            d = translate_row_col(r, c);
            if (d >= screen_num_desktops) {
                if (wrap)
                    --c;
                else
                    return from;
            }
            break;
        case OB_DIRECTION_SOUTH:
            ++r;
            if (r >= screen_desktop_layout.rows) {
                if (wrap)
                    r = 0;
                else
                    return from;
            }
            d = translate_row_col(r, c);
            if (d >= screen_num_desktops) {
                if (wrap)
                    ++r;
                else
                    return from;
            }
            break;
        case OB_DIRECTION_NORTH:
            --r;
            if (r >= screen_desktop_layout.rows) {
                if (wrap)
                    r = screen_desktop_layout.rows - 1;
                else
                    return from;
            }
            d = translate_row_col(r, c);
            if (d >= screen_num_desktops) {
                if (wrap)
                    --r;
                else
                    return from;
            }
            break;
        default:
            g_assert_not_reached();
            return from;
        }

        d = translate_row_col(r, c);
    }
    return d;
}

static gboolean screen_validate_layout(ObDesktopLayout *l)
{
    if (l->columns == 0 && l->rows == 0) /* both 0's is bad data.. */
        return FALSE;

    /* fill in a zero rows/columns */
    if (l->columns == 0) {
        l->columns = screen_num_desktops / l->rows;
        if (l->rows * l->columns < screen_num_desktops)
            l->columns++;
        if (l->rows * l->columns >= screen_num_desktops + l->columns)
            l->rows--;
    } else if (l->rows == 0) {
        l->rows = screen_num_desktops / l->columns;
        if (l->columns * l->rows < screen_num_desktops)
            l->rows++;
        if (l->columns * l->rows >= screen_num_desktops + l->rows)
            l->columns--;
    }

    /* bounds checking */
    if (l->orientation == OB_ORIENTATION_HORZ) {
        l->columns = MIN(screen_num_desktops, l->columns);
        l->rows = MIN(l->rows,
                      (screen_num_desktops + l->columns - 1) / l->columns);
        l->columns = screen_num_desktops / l->rows +
            !!(screen_num_desktops % l->rows);
    } else {
        l->rows = MIN(screen_num_desktops, l->rows);
        l->columns = MIN(l->columns,
                         (screen_num_desktops + l->rows - 1) / l->rows);
        l->rows = screen_num_desktops / l->columns +
            !!(screen_num_desktops % l->columns);
    }
    return TRUE;
}

void screen_update_layout(void)
{
    ObDesktopLayout l;

    screen_desktop_layout.orientation = OB_ORIENTATION_HORZ;
    screen_desktop_layout.start_corner = OB_CORNER_TOPLEFT;
    screen_desktop_layout.rows = 1;
    screen_desktop_layout.columns = screen_num_desktops;

    /* X pagers set _NET_DESKTOP_LAYOUT, in owbox it comes from rc.xml */
    if (config_desktops_rows > 0) {
        l.orientation = config_desktops_orientation;
        l.start_corner = config_desktops_start_corner;
        l.rows = config_desktops_rows;
        l.columns = config_desktops_columns;
        if (screen_validate_layout(&l))
            screen_desktop_layout = l;
    }
}

void screen_update_desktop_names(void)
{
    guint i;
    gchar **old = screen_desktop_names;

    /* keep names that were set at runtime (by a pager, or the IPC) */
    screen_desktop_names = g_new0(gchar*, screen_num_desktops + 1);
    i = 0;
    if (old)
        for (; old[i] && i < screen_num_desktops; ++i)
            screen_desktop_names[i] = g_strdup(old[i]);
    g_strfreev(old);

    if (i < screen_num_desktops) {
        GSList *it;

        it = g_slist_nth(config_desktops_names, i);

        for (; i < screen_num_desktops; ++i) {
            if (it && ((char*)it->data)[0]) /* not empty */
                /* use the names from the config file when possible */
                screen_desktop_names[i] = g_strdup(it->data);
            else
                /* make up a nice name if it's not though */
                screen_desktop_names[i] = g_strdup_printf(_("desktop %i"),
                                                          i + 1);
            if (it) it = g_slist_next(it);
        }
    }

    /* resize the pager for these names */
    for (i = 0; i < screen_num_monitors && desktop_popup; i++) {
        pager_popup_text_width_to_strings(desktop_popup[i],
                                          screen_desktop_names,
                                          screen_num_desktops);
    }

    toplevel_update_desktops();
}

void screen_set_desktop_name(guint desktop, const gchar *name)
{
    if (desktop >= screen_num_desktops || !screen_desktop_names) return;
    g_free(screen_desktop_names[desktop]);
    screen_desktop_names[desktop] = g_strdup(name);
    screen_update_desktop_names();
}

void screen_show_desktop(ObScreenShowDestopMode show_mode, ObClient *show_only)
{
    GList *it;

    ObScreenShowDestopMode before_mode = screen_show_desktop_mode;

    gboolean showing_before = screen_showing_desktop();
    screen_show_desktop_mode = show_mode;
    gboolean showing_after = screen_showing_desktop();

    if (showing_before == showing_after) {
        /* No change. */
        screen_show_desktop_mode = before_mode;
        return;
    }

    if (screen_show_desktop_mode == SCREEN_SHOW_DESKTOP_UNTIL_TOGGLE &&
        show_only != NULL)
    {
        /* If we're showing the desktop until the show-mode is toggled, we
           don't allow breaking out of showing-desktop mode unless we're
           showing all the windows again. */
        screen_show_desktop_mode = before_mode;
        return;
    }

    if (showing_after) {
        /* hide windows bottom to top */
        for (it = g_list_last(stacking_list); it; it = g_list_previous(it)) {
            if (WINDOW_IS_CLIENT(it->data)) {
                ObClient *client = it->data;
                client_showhide(client);
            }
        }
    }
    else {
        /* restore windows top to bottom */
        for (it = stacking_list; it; it = g_list_next(it)) {
            if (WINDOW_IS_CLIENT(it->data)) {
                ObClient *client = it->data;
                if (client_should_show(client)) {
                    if (!show_only || client == show_only)
                        client_show(client);
                    else
                        client_iconify(client, TRUE, FALSE, TRUE);
                }
            }
        }
    }

    if (showing_after) {
        /* focus the desktop */
        for (it = focus_order; it; it = g_list_next(it)) {
            ObClient *c = it->data;
            if (c->type == OB_CLIENT_TYPE_DESKTOP &&
                (c->desktop == screen_desktop || c->desktop == DESKTOP_ALL) &&
                client_focus(it->data))
                break;
        }
    }
    else if (!show_only) {
        ObClient *c;

        if ((c = focus_fallback(TRUE, FALSE, TRUE, FALSE))) {
            /* only do the flicker reducing stuff ahead of time if we are going
               to call xsetinputfocus on the window ourselves. otherwise there
               is no guarantee the window will actually take focus.. */
            if (c->can_focus) {
                /* reduce flicker by hiliting now rather than waiting for the
                   server FocusIn event */
                frame_adjust_focus(c->frame, TRUE);
            }
        }
    }

    toplevel_update_desktops();
}

gboolean screen_showing_desktop()
{
    switch (screen_show_desktop_mode) {
    case SCREEN_SHOW_DESKTOP_NO:
        return FALSE;
    case SCREEN_SHOW_DESKTOP_UNTIL_WINDOW:
    case SCREEN_SHOW_DESKTOP_UNTIL_TOGGLE:
        return TRUE;
    }
    g_assert_not_reached();
    return FALSE;
}

typedef struct {
    guint desktop;
    StrutPartial *strut;
} ObScreenStrut;

#define RESET_STRUT_LIST(sl) \
    while (sl) { \
        g_slice_free(ObScreenStrut, (sl)->data); \
        sl = g_slist_delete_link(sl, sl); \
    }

#define ADD_STRUT_TO_LIST(sl, d, s) \
{ \
    ObScreenStrut *ss = g_slice_new(ObScreenStrut); \
    ss->desktop = d; \
    ss->strut = s;  \
    sl = g_slist_prepend(sl, ss); \
}

#define VALIDATE_STRUTS(sl, side, max) \
{ \
    GSList *it; \
    for (it = sl; it; it = g_slist_next(it)) { \
      ObScreenStrut *ss = it->data; \
      ss->strut->side = MIN(max, ss->strut->side); \
    } \
}

static void get_xinerama_screens(Rect **xin_areas, guint *nxin)
{
    guint i;
    gint l, r, t, b;

    /* the monitors are the enabled outputs, in layout order */
    *nxin = output_count();
    if (*nxin == 0) {
        /* no outputs (yet), pretend to have a screen so things work */
        *nxin = 1;
        *xin_areas = g_new(Rect, *nxin + 1);
        RECT_SET((*xin_areas)[0], 0, 0, 1024, 768);
    } else {
        *xin_areas = g_new(Rect, *nxin + 1);
        for (i = 0; i < *nxin; ++i)
            (*xin_areas)[i] = output_nth(i)->area;
    }

    /* returns one extra with the total area in it */
    l = (*xin_areas)[0].x;
    t = (*xin_areas)[0].y;
    r = (*xin_areas)[0].x + (*xin_areas)[0].width - 1;
    b = (*xin_areas)[0].y + (*xin_areas)[0].height - 1;
    for (i = 1; i < *nxin; ++i) {
        l = MIN(l, (*xin_areas)[i].x);
        t = MIN(t, (*xin_areas)[i].y);
        r = MAX(r, (*xin_areas)[i].x + (*xin_areas)[i].width - 1);
        b = MAX(b, (*xin_areas)[i].y + (*xin_areas)[i].height - 1);
    }
    RECT_SET((*xin_areas)[*nxin], l, t, r - l + 1, b - t + 1);

    for (i = 0; i < *nxin; ++i)
        ob_debug("Monitor %d @ %d,%d %dx%d", i,
                 (*xin_areas)[i].x, (*xin_areas)[i].y,
                 (*xin_areas)[i].width, (*xin_areas)[i].height);
    ob_debug("Full desktop @ %d,%d %dx%d",
             (*xin_areas)[i].x, (*xin_areas)[i].y,
             (*xin_areas)[i].width, (*xin_areas)[i].height);
}

void screen_update_areas(void)
{
    guint old_num_monitors = screen_num_monitors;
    GList *it, *onscreen;

    /* collect the clients that are on screen */
    onscreen = NULL;
    for (it = client_list; it; it = g_list_next(it)) {
        if (client_monitor(it->data) != screen_num_monitors)
            onscreen = g_list_prepend(onscreen, it->data);
    }

    g_free(monitor_area);
    get_xinerama_screens(&monitor_area, &screen_num_monitors);
    if (screen_num_monitors != old_num_monitors) {
        if (desktop_popup)
            desktop_popup_free(old_num_monitors);
        desktop_popup_new();
    }

    /* set up the user-specified margins */
    config_margins.top_start = RECT_LEFT(monitor_area[screen_num_monitors]);
    config_margins.top_end = RECT_RIGHT(monitor_area[screen_num_monitors]);
    config_margins.bottom_start = RECT_LEFT(monitor_area[screen_num_monitors]);
    config_margins.bottom_end = RECT_RIGHT(monitor_area[screen_num_monitors]);
    config_margins.left_start = RECT_TOP(monitor_area[screen_num_monitors]);
    config_margins.left_end = RECT_BOTTOM(monitor_area[screen_num_monitors]);
    config_margins.right_start = RECT_TOP(monitor_area[screen_num_monitors]);
    config_margins.right_end = RECT_BOTTOM(monitor_area[screen_num_monitors]);

    RESET_STRUT_LIST(struts_left);
    RESET_STRUT_LIST(struts_top);
    RESET_STRUT_LIST(struts_right);
    RESET_STRUT_LIST(struts_bottom);

    /* collect the struts */
    for (it = client_list; it; it = g_list_next(it)) {
        ObClient *c = it->data;
        if (c->strut.left)
            ADD_STRUT_TO_LIST(struts_left, c->desktop, &c->strut);
        if (c->strut.top)
            ADD_STRUT_TO_LIST(struts_top, c->desktop, &c->strut);
        if (c->strut.right)
            ADD_STRUT_TO_LIST(struts_right, c->desktop, &c->strut);
        if (c->strut.bottom)
            ADD_STRUT_TO_LIST(struts_bottom, c->desktop, &c->strut);
    }
    /* layer shell surfaces (panels) reserve space like docks do */
    for (it = layers_struts(); it; it = g_list_next(it)) {
        StrutPartial *s = it->data;
        if (s->left)
            ADD_STRUT_TO_LIST(struts_left, DESKTOP_ALL, s);
        if (s->top)
            ADD_STRUT_TO_LIST(struts_top, DESKTOP_ALL, s);
        if (s->right)
            ADD_STRUT_TO_LIST(struts_right, DESKTOP_ALL, s);
        if (s->bottom)
            ADD_STRUT_TO_LIST(struts_bottom, DESKTOP_ALL, s);
    }

    if (config_margins.left)
        ADD_STRUT_TO_LIST(struts_left, DESKTOP_ALL, &config_margins);
    if (config_margins.top)
        ADD_STRUT_TO_LIST(struts_top, DESKTOP_ALL, &config_margins);
    if (config_margins.right)
        ADD_STRUT_TO_LIST(struts_right, DESKTOP_ALL, &config_margins);
    if (config_margins.bottom)
        ADD_STRUT_TO_LIST(struts_bottom, DESKTOP_ALL, &config_margins);

    VALIDATE_STRUTS(struts_left, left,
                    monitor_area[screen_num_monitors].width / 2);
    VALIDATE_STRUTS(struts_right, right,
                    monitor_area[screen_num_monitors].width / 2);
    VALIDATE_STRUTS(struts_top, top,
                    monitor_area[screen_num_monitors].height / 2);
    VALIDATE_STRUTS(struts_bottom, bottom,
                    monitor_area[screen_num_monitors].height / 2);

    xwayland_update_workareas();

    /* the area has changed, adjust all the windows if they need it */
    for (it = onscreen; it; it = g_list_next(it))
        client_reconfigure(it->data, FALSE);

    g_list_free(onscreen);
}

#if 0
Rect* screen_area_all_monitors(guint desktop)
{
    guint i;
    Rect *a;

    a = screen_area_monitor(desktop, 0);

    /* combine all the monitors together */
    for (i = 1; i < screen_num_monitors; ++i) {
        Rect *m = screen_area_monitor(desktop, i);
        gint l, r, t, b;

        l = MIN(RECT_LEFT(*a), RECT_LEFT(*m));
        t = MIN(RECT_TOP(*a), RECT_TOP(*m));
        r = MAX(RECT_RIGHT(*a), RECT_RIGHT(*m));
        b = MAX(RECT_BOTTOM(*a), RECT_BOTTOM(*m));

        RECT_SET(*a, l, t, r - l + 1, b - t + 1);

        g_free(m);
    }

    return a;
}
#endif

#define STRUT_LEFT_IN_SEARCH(s, search) \
    (RANGES_INTERSECT(search->y, search->height, \
                      s->left_start, s->left_end - s->left_start + 1))
#define STRUT_RIGHT_IN_SEARCH(s, search) \
    (RANGES_INTERSECT(search->y, search->height, \
                      s->right_start, s->right_end - s->right_start + 1))
#define STRUT_TOP_IN_SEARCH(s, search) \
    (RANGES_INTERSECT(search->x, search->width, \
                      s->top_start, s->top_end - s->top_start + 1))
#define STRUT_BOTTOM_IN_SEARCH(s, search) \
    (RANGES_INTERSECT(search->x, search->width, \
                      s->bottom_start, s->bottom_end - s->bottom_start + 1))

#define STRUT_LEFT_IGNORE(s, us, search) \
    (head == SCREEN_AREA_ALL_MONITORS && us && \
     RECT_LEFT(monitor_area[i]) + s->left > RECT_LEFT(*search))
#define STRUT_RIGHT_IGNORE(s, us, search) \
    (head == SCREEN_AREA_ALL_MONITORS && us && \
     RECT_RIGHT(monitor_area[i]) - s->right < RECT_RIGHT(*search))
#define STRUT_TOP_IGNORE(s, us, search) \
    (head == SCREEN_AREA_ALL_MONITORS && us && \
     RECT_TOP(monitor_area[i]) + s->top > RECT_TOP(*search))
#define STRUT_BOTTOM_IGNORE(s, us, search) \
    (head == SCREEN_AREA_ALL_MONITORS && us && \
     RECT_BOTTOM(monitor_area[i]) - s->bottom < RECT_BOTTOM(*search))

Rect* screen_area(guint desktop, guint head, Rect *search)
{
    Rect *a;
    GSList *it;
    gint l, r, t, b;
    guint i, d;
    gboolean us = search != NULL; /* user provided search */

    g_assert(desktop < screen_num_desktops || desktop == DESKTOP_ALL);
    g_assert(head < screen_num_monitors || head == SCREEN_AREA_ONE_MONITOR ||
             head == SCREEN_AREA_ALL_MONITORS);
    g_assert(!(head == SCREEN_AREA_ONE_MONITOR && search == NULL));

    /* find any struts for this monitor
       which will be affecting the search area.
    */

    /* search everything if search is null */
    if (!search) {
        if (head < screen_num_monitors) search = &monitor_area[head];
        else search = &monitor_area[screen_num_monitors];
    }
    if (head == SCREEN_AREA_ONE_MONITOR) head = screen_find_monitor(search);

    /* al is "all left" meaning the furthest left you can get, l is our
       "working left" meaning our current strut edge which we're calculating
    */

    /* only include monitors which the search area lines up with */
    if (RECT_INTERSECTS_RECT(monitor_area[screen_num_monitors], *search)) {
        l = RECT_RIGHT(monitor_area[screen_num_monitors]);
        t = RECT_BOTTOM(monitor_area[screen_num_monitors]);
        r = RECT_LEFT(monitor_area[screen_num_monitors]);
        b = RECT_TOP(monitor_area[screen_num_monitors]);
        for (i = 0; i < screen_num_monitors; ++i) {
            /* add the monitor if applicable */
            if (RANGES_INTERSECT(search->x, search->width,
                                 monitor_area[i].x, monitor_area[i].width))
            {
                t = MIN(t, RECT_TOP(monitor_area[i]));
                b = MAX(b, RECT_BOTTOM(monitor_area[i]));
            }
            if (RANGES_INTERSECT(search->y, search->height,
                                 monitor_area[i].y, monitor_area[i].height))
            {
                l = MIN(l, RECT_LEFT(monitor_area[i]));
                r = MAX(r, RECT_RIGHT(monitor_area[i]));
            }
        }
    } else {
        l = RECT_LEFT(monitor_area[screen_num_monitors]);
        t = RECT_TOP(monitor_area[screen_num_monitors]);
        r = RECT_RIGHT(monitor_area[screen_num_monitors]);
        b = RECT_BOTTOM(monitor_area[screen_num_monitors]);
    }

    for (d = 0; d < screen_num_desktops; ++d) {
        if (d != desktop && desktop != DESKTOP_ALL) continue;

        for (i = 0; i < screen_num_monitors; ++i) {
            if (head != SCREEN_AREA_ALL_MONITORS && head != i) continue;

            for (it = struts_left; it; it = g_slist_next(it)) {
                ObScreenStrut *s = it->data;
                if ((s->desktop == d || s->desktop == DESKTOP_ALL) &&
                    STRUT_LEFT_IN_SEARCH(s->strut, search) &&
                    !STRUT_LEFT_IGNORE(s->strut, us, search))
                    l = MAX(l, RECT_LEFT(monitor_area[screen_num_monitors])
                               + s->strut->left);
            }
            for (it = struts_top; it; it = g_slist_next(it)) {
                ObScreenStrut *s = it->data;
                if ((s->desktop == d || s->desktop == DESKTOP_ALL) &&
                    STRUT_TOP_IN_SEARCH(s->strut, search) &&
                    !STRUT_TOP_IGNORE(s->strut, us, search))
                    t = MAX(t, RECT_TOP(monitor_area[screen_num_monitors])
                               + s->strut->top);
            }
            for (it = struts_right; it; it = g_slist_next(it)) {
                ObScreenStrut *s = it->data;
                if ((s->desktop == d || s->desktop == DESKTOP_ALL) &&
                    STRUT_RIGHT_IN_SEARCH(s->strut, search) &&
                    !STRUT_RIGHT_IGNORE(s->strut, us, search))
                    r = MIN(r, RECT_RIGHT(monitor_area[screen_num_monitors])
                               - s->strut->right);
            }
            for (it = struts_bottom; it; it = g_slist_next(it)) {
                ObScreenStrut *s = it->data;
                if ((s->desktop == d || s->desktop == DESKTOP_ALL) &&
                    STRUT_BOTTOM_IN_SEARCH(s->strut, search) &&
                    !STRUT_BOTTOM_IGNORE(s->strut, us, search))
                    b = MIN(b, RECT_BOTTOM(monitor_area[screen_num_monitors])
                               - s->strut->bottom);
            }

            /* limit to this monitor */
            if (head == i) {
                l = MAX(l, RECT_LEFT(monitor_area[i]));
                t = MAX(t, RECT_TOP(monitor_area[i]));
                r = MIN(r, RECT_RIGHT(monitor_area[i]));
                b = MIN(b, RECT_BOTTOM(monitor_area[i]));
            }
        }
    }

    a = g_slice_new(Rect);
    a->x = l;
    a->y = t;
    a->width = r - l + 1;
    a->height = b - t + 1;
    return a;
}

typedef struct {
    Rect r;
    gboolean subtract;
} RectArithmetic;

guint screen_find_monitor(const Rect *search)
{
    guint i;
    guint mostpx_index = screen_num_monitors;
    glong mostpx = 0;
    guint closest_distance_index = screen_num_monitors;
    guint closest_distance = G_MAXUINT;
    GSList *counted = NULL;

    /* we want to count the number of pixels search has on each monitor, but not
       double count.  so if a pixel is counted on monitor A then we should not
       count it again on monitor B. in the end we want to return the monitor
       that had the most pixels counted under this scheme.

       this assumes that monitors earlier in the list are more desirable to be
       considered the search area's monitor.  we try the configured primary
       monitor first, so it gets the highest preference.

       if we have counted an area A, then we want to subtract the intersection
       of A with the area on future monitors.
       but now consider if we count an area B that intersects A. we want to
       subtract the area B from that counted on future monitors, but not
       subtract the intersection of A and B twice! so we would add the
       intersection of A and B back, to account for it being subtracted both
       for A and B.

       this is the idea behind the algorithm.  we always subtract the full area
       for monitor M intersected with the search area. we'll call that AREA.
       but then we go through the list |counted| and for each rectangle in
       the list that is being subtracted from future monitors, we insert a
       request to add back the intersection of the subtracted rect with AREA.
       vice versa for a rect in |counted| that is getting added back.
    */

    if (config_primary_monitor_index < screen_num_monitors) {
        const Rect *monitor;
        Rect on_current_monitor;
        glong area;

        monitor = screen_physical_area_monitor(config_primary_monitor_index);

        if (RECT_INTERSECTS_RECT(*monitor, *search)) {
            RECT_SET_INTERSECTION(on_current_monitor, *monitor, *search);
            area = RECT_AREA(on_current_monitor);

            if (area > mostpx) {
                mostpx = area;
                mostpx_index = config_primary_monitor_index;
            }

            /* add the intersection rect on the current monitor to the
               counted list. that's easy for the first one, we just mark it for
               subtraction */
            {
                RectArithmetic *ra = g_slice_new(RectArithmetic);
                ra->r = on_current_monitor;
                ra->subtract = TRUE;
                counted = g_slist_prepend(counted, ra);
            }
        }
    }

    for (i = 0; i < screen_num_monitors; ++i) {
        const Rect *monitor;
        Rect on_current_monitor;
        glong area;
        GSList *it;

        monitor = screen_physical_area_monitor(i);

        if (!RECT_INTERSECTS_RECT(*monitor, *search)) {
            /* If we don't intersect then find the distance between the search
               rect and the monitor. We'll use the closest monitor from this
               metric if none of the monitors intersect. */
            guint distance = rect_manhatten_distance(*monitor, *search);

            if (distance < closest_distance) {
                closest_distance = distance;
                closest_distance_index = i;
            }
            continue;
        }

        if (i == config_primary_monitor_index)
            continue;  /* already did this one */

        RECT_SET_INTERSECTION(on_current_monitor, *monitor, *search);
        area = RECT_AREA(on_current_monitor);

        /* remove pixels we already counted on any previous monitors. */
        for (it = counted; it; it = g_slist_next(it)) {
            RectArithmetic *ra = it->data;
            Rect intersection;

            RECT_SET_INTERSECTION(intersection, ra->r, *search);
            if (ra->subtract) area -= RECT_AREA(intersection);
            else area += RECT_AREA(intersection);
        }

        if (area > mostpx) {
            mostpx = area;
            mostpx_index = i;
        }

        /* add the intersection rect on the current monitor I to the counted
           list.
           but now we need to compensate for every rectangle R already in the
           counted list, and add a new rect R' that is the intersection of
           R and I, but with the reverse subtraction/addition operation.
        */
        for (it = counted; it; it = g_slist_next(it)) {
            RectArithmetic *saved = it->data;

            if (!RECT_INTERSECTS_RECT(saved->r, on_current_monitor))
                continue;
            /* we are going to subtract our rect from future monitors, but
               part of it may already be being subtracted/added, so compensate
               to not double add/subtract. */
            RectArithmetic *reverse = g_slice_new(RectArithmetic);
            RECT_SET_INTERSECTION(reverse->r, saved->r, on_current_monitor);
            reverse->subtract = !saved->subtract;
            /* prepend so we can continue thru the list uninterupted */
            counted = g_slist_prepend(counted, reverse);
        }
        {
            RectArithmetic *ra = g_slice_new(RectArithmetic);
            ra->r = on_current_monitor;
            ra->subtract = TRUE;
            counted = g_slist_prepend(counted, ra);
        }
    }

    while (counted) {
        g_slice_free(RectArithmetic, counted->data);
        counted = g_slist_delete_link(counted, counted);
    }

    if (mostpx_index < screen_num_monitors)
        return mostpx_index;

    g_assert(closest_distance_index < screen_num_monitors);
    return closest_distance_index;
}

const Rect* screen_physical_area_all_monitors(void)
{
    return screen_physical_area_monitor(screen_num_monitors);
}

const Rect* screen_physical_area_monitor(guint head)
{
    g_assert(head <= screen_num_monitors);

    return &monitor_area[head];
}

gboolean screen_physical_area_monitor_contains(guint head, Rect *search)
{
    g_assert(head <= screen_num_monitors);
    g_assert(search);
    return RECT_INTERSECTS_RECT(monitor_area[head], *search);
}

guint screen_monitor_active(void)
{
    if (moveresize_client)
        return client_monitor(moveresize_client);
    else if (focus_client)
        return client_monitor(focus_client);
    else
        return screen_monitor_pointer();
}

/* owbox: which of the pointer and the focus moved last */
static gboolean pointer_moved_last = TRUE;

guint screen_monitor_current(void)
{
    if (!pointer_moved_last && focus_client)
        return client_monitor(focus_client);
    else
        return screen_monitor_pointer();
}

void screen_pointer_moved(void)
{
    pointer_moved_last = TRUE;
}

void screen_focus_moved(void)
{
    pointer_moved_last = FALSE;
}

const Rect* screen_physical_area_active(void)
{
    return screen_physical_area_monitor(screen_monitor_active());
}

guint screen_monitor_primary(gboolean fixed)
{
    if (config_primary_monitor_index > 0) {
        if (config_primary_monitor_index-1 < screen_num_monitors)
            return config_primary_monitor_index - 1;
        else
            return 0;
    }
    else if (fixed)
        return 0;
    else if (config_primary_monitor == OB_PLACE_MONITOR_ACTIVE)
        return screen_monitor_active();
    else if (config_primary_monitor == OB_PLACE_MONITOR_CURRENT)
        return screen_monitor_current();
    else /* config_primary_monitor == OB_PLACE_MONITOR_MOUSE */
        return screen_monitor_pointer();
}

const Rect* screen_physical_area_primary(gboolean fixed)
{
    return screen_physical_area_monitor(screen_monitor_primary(fixed));
}

void screen_set_root_cursor(void)
{
    seat_update_cursor();
}

guint screen_find_monitor_point(guint x, guint y)
{
    Rect mon;
    RECT_SET(mon, x, y, 1, 1);
    return screen_find_monitor(&mon);
}

guint screen_monitor_pointer()
{
    gint x, y;
    if (!screen_pointer_pos(&x, &y))
        x = y = 0;
    return screen_find_monitor_point(x, y);
}

gboolean screen_pointer_pos(gint *x, gint *y)
{
    seat_pointer_pos(x, y);
    return TRUE;
}

gboolean screen_compare_desktops(guint a, guint b)
{
    if (a == DESKTOP_ALL)
        a = screen_desktop;
    if (b == DESKTOP_ALL)
        b = screen_desktop;
    return a == b;
}

void screen_apply_gravity_point(gint *x, gint *y, gint width, gint height,
                                const GravityPoint *position, const Rect *area)
{
    if (position->x.center)
        *x = area->width / 2 - width / 2;
    else {
        *x = position->x.pos;
        if (position->x.denom)
            *x = (*x * area->width) / position->x.denom;
        if (position->x.opposite)
            *x = area->width - width - *x;
    }

    if (position->y.center)
        *y = area->height / 2 - height / 2;
    else {
        *y = position->y.pos;
        if (position->y.denom)
            *y = (*y * area->height) / position->y.denom;
        if (position->y.opposite)
            *y = area->height - height - *y;
    }

    *x += area->x;
    *y += area->y;
}
