/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   share_menu.c for owbox
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

/* A menu to choose what to share when an application wants to capture the
   screen.  xdg-desktop-portal-wlr runs "owbox --choose-share", which asks
   owbox for this menu over the control socket, and prints the choice:
   "Monitor: <output>" or "Window: <ext-foreign-toplevel identifier>". */

#include "share_menu.h"
#include "menu.h"
#include "menuframe.h"
#include "client.h"
#include "stacking.h"
#include "screen.h"
#include "output.h"
#include "server.h"
#include "toplevel.h"
#include "config.h"
#include "gettext.h"

#include <wlr/types/wlr_output.h>

#define MENU_NAME "owbox-share-menu"

static ObMenu *share_menu;
/*! What each entry shares, indexed by the entry's id */
static GPtrArray *choices;
/*! The choice made, while the menu closes */
static gchar *choice;
static ObShareMenuDone done_func;
static gpointer done_data;
static guint finish_id;

static gboolean self_update(ObMenuFrame *frame, gpointer data)
{
    ObMenu *menu = frame->menu;
    ObOutput *o;
    GList *it;

    menu_clear_entries(menu);
    g_ptr_array_set_size(choices, 0);

    menu_add_separator(menu, -1, _("Share the screen"));
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_output *w = o->wlr_output;
        gchar *label;
        if (!w->enabled) continue;
        label = w->model ?
            g_strdup_printf(_("Entire screen: %s (%s)"), w->name, w->model) :
            g_strdup_printf(_("Entire screen: %s"), w->name);
        menu_add_normal(menu, choices->len, label, NULL, FALSE);
        g_ptr_array_add(choices, g_strdup_printf("Monitor: %s", w->name));
        g_free(label);
    }

    menu_add_separator(menu, -1, _("Share a window"));
    /* the windows from the top down, like the Alt-Tab list */
    for (it = stacking_list; it; it = g_list_next(it)) {
        ObClient *c;
        ObMenuEntry *e;
        const gchar *id;

        if (!WINDOW_IS_CLIENT(it->data)) continue;
        c = it->data;
        if (!(id = toplevel_identifier(c)) || !client_normal(c)) continue;
        e = menu_add_normal(menu, choices->len,
                            c->iconic ? c->icon_title : c->title, NULL,
                            FALSE);
        if (config_menu_show_icons) {
            e->data.normal.icon = client_icon(c);
            RrImageRef(e->data.normal.icon);
            e->data.normal.icon_alpha = 0xff;
        }
        g_ptr_array_add(choices, g_strdup_printf("Window: %s", id));
    }
    return TRUE;
}

static gboolean finish(gpointer data)
{
    ObShareMenuDone func = done_func;
    gchar *c = choice;

    finish_id = 0;
    done_func = NULL;
    choice = NULL;
    if (func)
        func(c, done_data);
    g_free(c);
    return G_SOURCE_REMOVE;
}

static void menu_execute(ObMenuEntry *self, ObMenuFrame *f, ObClient *c,
                         guint state, gpointer data)
{
    if (self->id >= 0 && (guint)self->id < choices->len) {
        g_free(choice);
        choice = g_strdup(choices->pdata[self->id]);
    }
}

static void menu_hide(ObMenuFrame *frame, gpointer data)
{
    /* an entry's execute comes after the menu hides, answer after both */
    if (!finish_id)
        finish_id = g_idle_add(finish, NULL);
}

void share_menu_choose(ObShareMenuDone func, gpointer data)
{
    GravityPoint pos;
    gint x, y;

    /* only one question at a time, the older one is declined */
    if (done_func) {
        if (finish_id) g_source_remove(finish_id);
        finish(NULL);
    }
    done_func = func;
    done_data = data;

    screen_pointer_pos(&x, &y);
    GRAVITY_COORD_SET(pos.x, x - screen_physical_area_all_monitors()->x,
                      FALSE, FALSE);
    GRAVITY_COORD_SET(pos.y, y - screen_physical_area_all_monitors()->y,
                      FALSE, FALSE);
    pos.x.denom = pos.y.denom = 0;
    menu_show(MENU_NAME, &pos, screen_num_monitors, FALSE, FALSE, NULL);

    /* it couldn't be shown (another menu or a grab is active) */
    if (!menu_frame_visible ||
        ((ObMenuFrame*)menu_frame_visible->data)->menu != share_menu)
        menu_hide(NULL, NULL);
}

void share_menu_startup(gboolean reconfig)
{
    choices = g_ptr_array_new_with_free_func(g_free);
    share_menu = menu_new(MENU_NAME, _("Share"), FALSE, NULL);
    menu_set_update_func(share_menu, self_update);
    menu_set_execute_func(share_menu, menu_execute);
    menu_set_hide_func(share_menu, menu_hide);
}

void share_menu_shutdown(gboolean reconfig)
{
    /* the menu is destroyed with the others */
    g_ptr_array_free(choices, TRUE);
    choices = NULL;
}
