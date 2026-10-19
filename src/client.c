/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   client.c for owbox (ported from the Openbox window manager)
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

#include "client.h"
#include "debug.h"
#include "screen.h"
#include "moveresize.h"
#include "place.h"
#include "frame.h"
#include "event.h"
#include "grab.h"
#include "prompt.h"
#include "focus.h"
#include "focus_cycle.h"
#include "stacking.h"
#include "openbox.h"
#include "config.h"
#include "menuframe.h"
#include "keyboard.h"
#include "mouse.h"
#include "seat.h"
#include "server.h"
#include "toplevel.h"
#include "gravity.h"
#include "render/render.h"
#include "gettext.h"

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_SIGNAL_H
#  include <signal.h> /* for kill() */
#endif

#include <glib.h>
#include <string.h>

/* ICCCM WM_STATE values, kept to track if a window is visible */
#define WithdrawnState 0
#define NormalState    1
#define IconicState    3

/*! How long to wait for a client to draw itself at a new size before
  showing the frame at that size anyways (milliseconds) */
#define CONFIGURE_TIMEOUT 200

typedef struct
{
    ObClientCallback func;
    gpointer data;
} ClientCallback;

GList          *client_list             = NULL;

static GSList  *client_destroy_notifies = NULL;
static RrImage *client_default_icon     = NULL;

static void client_get_all(ObClient *self, gboolean real);
static void client_get_session_ids(ObClient *self);
static void client_get_area(ObClient *self);
static void client_get_desktop(ObClient *self);
static void client_get_state(ObClient *self);
static void client_set_desktop_recursive(ObClient *self,
                                         guint target,
                                         gboolean donthide,
                                         gboolean dontraise);
static void client_change_allowed_actions(ObClient *self);
static void client_change_state(ObClient *self);
static void client_change_wm_state(ObClient *self);
static void client_apply_startup_state(ObClient *self,
                                       gint x, gint y, gint w, gint h);
static ObAppSettings *client_get_settings_state(ObClient *self);
static void client_update_transient_tree(ObClient *self,
                                         ObClient* oldparent,
                                         ObClient *newparent);
static void client_present(ObClient *self, gboolean here, gboolean raise,
                           gboolean unshade);
static GSList *client_search_all_top_parents_internal(ObClient *self,
                                                      gboolean bylayer,
                                                      ObStackingLayer layer);
static void client_call_notifies(ObClient *self, GSList *list);
static void client_prompt_kill(ObClient *self);
static gboolean client_can_steal_focus(ObClient *self,
                                       gboolean allow_other_desktop,
                                       gboolean request_from_user,
                                       guint32 steal_time,
                                       guint32 launch_time);
static void client_setup_default_decor_and_functions(ObClient *self);
static void client_setup_decor_undecorated(ObClient *self);
static void client_apply_configure(ObClient *self, gint x, gint y,
                                   gint w, gint h, gboolean user,
                                   gboolean final);

void client_startup(gboolean reconfig)
{
    client_default_icon = RrImageNewFromData(
        ob_rr_icons, ob_rr_theme->def_win_icon,
        ob_rr_theme->def_win_icon_w, ob_rr_theme->def_win_icon_h);

    if (reconfig) return;

    client_set_list();
}

void client_shutdown(gboolean reconfig)
{
    RrImageUnref(client_default_icon);
    client_default_icon = NULL;

    if (reconfig) return;
}

static void client_call_notifies(ObClient *self, GSList *list)
{
    GSList *it;

    for (it = list; it; it = g_slist_next(it)) {
        ClientCallback *d = it->data;
        d->func(self, d->data);
    }
}

void client_add_destroy_notify(ObClientCallback func, gpointer data)
{
    ClientCallback *d = g_slice_new(ClientCallback);
    d->func = func;
    d->data = data;
    client_destroy_notifies = g_slist_prepend(client_destroy_notifies, d);
}

void client_remove_destroy_notify(ObClientCallback func)
{
    GSList *it;

    for (it = client_destroy_notifies; it; it = g_slist_next(it)) {
        ClientCallback *d = it->data;
        if (d->func == func) {
            g_slice_free(ClientCallback, d);
            client_destroy_notifies =
                g_slist_delete_link(client_destroy_notifies, it);
            break;
        }
    }
}

void client_remove_destroy_notify_data(ObClientCallback func, gpointer data)
{
    GSList *it;

    for (it = client_destroy_notifies; it; it = g_slist_next(it)) {
        ClientCallback *d = it->data;
        if (d->func == func && d->data == data) {
            g_slice_free(ClientCallback, d);
            client_destroy_notifies =
                g_slist_delete_link(client_destroy_notifies, it);
            break;
        }
    }
}

void client_set_list(void)
{
    stacking_set_list();
}

ObClient* client_new(const ObClientImpl *impl, gpointer impl_data,
                     struct wlr_scene_tree *surface_tree)
{
    ObClient *self;

    self = g_slice_new0(ObClient);
    self->obwin.type = OB_WINDOW_CLASS_CLIENT;
    self->node.type = OB_NODE_WINDOW;
    self->node.owner = CLIENT_AS_WINDOW(self);
    self->impl = impl;
    self->impl_data = impl_data;
    self->surface_tree = surface_tree;
    ob_node_attach(&self->node, &surface_tree->node);
    return self;
}

void client_free(ObClient *self)
{
    g_assert(!self->managed);
    if (self->surface_tree)
        self->surface_tree->node.data = NULL;
    g_slice_free(ObClient, self);
}

void client_manage(ObClient *self)
{
    gboolean try_activate = FALSE;
    gboolean do_activate;
    ObAppSettings *settings;
    gboolean transient = FALSE;
    Rect place;
    guint32 launch_time = 0;
    guint32 user_time;
    gboolean obplaced;
    gulong ignore_start = FALSE;

    g_assert(!self->managed);
    self->managed = TRUE;

    /* non-zero defaults */
    self->wmstate = WithdrawnState; /* make sure it gets updated first time */
    self->gravity = NorthWestGravity;
    self->desktop = screen_num_desktops; /* always an invalid value */

    /* get all the stuff off the window */
    client_get_all(self, TRUE);

    ob_debug("Window type: %d", self->type);
    ob_debug("Window name: %s class: %s role: %s title: %s",
             self->name, self->class, self->role, self->title);

    /* per-app settings override stuff from client_get_all, and return the
       settings for other uses too. the returned settings is a shallow copy,
       that needs to be freed with g_free(). */
    settings = client_get_settings_state(self);

    /* the per-app settings/session may have changed the decorations for
       the window, so we setup decorations for that here.  this is a special
       case because we want to place the window according to these decoration
       changes.
       we do this before setting up the frame so that it will reflect the
       decorations of the window as it will be placed on screen.
    */
    client_setup_decor_undecorated(self);

    /* create the decoration frame for the client window */
    self->frame = frame_new(self);

    frame_grab_client(self->frame);

    /* don't put helper/modal windows on a different desktop if they are
       related to the focused window.  */
    if (!screen_compare_desktops(self->desktop, screen_desktop) &&
        focus_client && client_search_transient(focus_client, self)  &&
        (client_helper(self) || self->modal))
    {
        self->desktop = screen_desktop;
    }

    /* tell startup notification that this app started */
    launch_time = toplevel_activation_time(self);

    user_time = event_time();

    /* do this after we have a frame.. it uses the frame to help determine the
       WM_STATE to apply. */
    client_change_state(self);

    /* add ourselves to the focus order */
    focus_order_add_new(self);

    /* do this to add ourselves to the stacking list in a non-intrusive way */
    client_calc_layer(self);

    /* focus the new window? */
    if (ob_state() != OB_STATE_STARTING &&
        /* this means focus=true for window is same as config_focus_new=true */
        ((config_focus_new || settings->focus == 1) ||
         client_search_focus_tree_full(self)) &&
        /* NET_WM_USER_TIME 0 when mapping means don't focus */
        (user_time != 0) &&
        /* this checks for focus=false for the window */
        settings->focus != 0 &&
        focus_valid_target(self, self->desktop,
                           FALSE, FALSE, TRUE, TRUE, FALSE, FALSE,
                           settings->focus == 1))
    {
        try_activate = TRUE;
    }

    /* adjust the frame to the client's size before showing or placing
       the window */
    frame_adjust_area(self->frame, FALSE, TRUE, FALSE);
    frame_adjust_client_area(self->frame);

    /* where the frame was placed is where the window was originally */
    place = self->area;

    ob_debug("Going to try activate new window? %s",
             try_activate ? "yes" : "no");
    if (try_activate)
        do_activate = client_can_steal_focus(
            self, settings->focus == 1,
            (!!launch_time || settings->focus == 1),
            event_time(), launch_time);
    else
        do_activate = FALSE;

    /* figure out placement for the window if the window is new */
    if (ob_state() == OB_STATE_RUNNING) {
        ob_debug("Positioned: %s @ %d %d",
                 (!self->positioned ? "no" :
                  (self->positioned == PPosition ? "program specified" :
                   (self->positioned == USPosition ? "user specified" :
                    (self->positioned == (PPosition | USPosition) ?
                     "program + user specified" :
                     "BADNESS !?")))), place.x, place.y);

        ob_debug("Sized: %s @ %d %d",
                 (!self->sized ? "no" :
                  (self->sized == PSize ? "program specified" :
                   (self->sized == USSize ? "user specified" :
                    (self->sized == (PSize | USSize) ?
                     "program + user specified" :
                     "BADNESS !?")))), place.width, place.height);

        obplaced = place_client(self, do_activate, &place, settings);

        /* watch for buggy apps that ask to be placed at (0,0) when there is
           a strut there */
        if (!obplaced && place.x == 0 && place.y == 0 &&
            /* non-normal windows are allowed */
            client_normal(self) &&
            /* oldschool fullscreen windows are allowed */
            !client_is_oldfullscreen(self, &place))
        {
            Rect *r;

            r = screen_area(self->desktop, SCREEN_AREA_ALL_MONITORS, NULL);
            if (r->x || r->y) {
                place.x = r->x;
                place.y = r->y;
                ob_debug("Moving buggy app from (0,0) to (%d,%d)", r->x, r->y);
            }
            g_slice_free(Rect, r);
        }

        /* make sure the window is visible. */
        client_find_onscreen(self, &place.x, &place.y,
                             place.width, place.height,
                             /* non-normal clients has less rules, and
                                windows that are being restored from a
                                session do also. we can assume you want
                                it back where you saved it. Clients saying
                                they placed themselves are subjected to
                                harder rules, ones that are placed by
                                place.c or by the user are allowed partially
                                off-screen and on xinerama divides (ie,
                                it is up to the placement routines to avoid
                                the xinerama divides)

                                children and splash screens are forced on
                                screen, but i don't remember why i decided to
                                do that.
                             */
                             ob_state() == OB_STATE_RUNNING &&
                             (self->type == OB_CLIENT_TYPE_DIALOG ||
                              self->type == OB_CLIENT_TYPE_SPLASH ||
                              (!((self->positioned & USPosition) ||
                                 settings->pos_given) &&
                               client_normal(self) &&
                               /* don't move oldschool fullscreen windows to
                                  fit inside the struts (fixes Acroread, which
                                  makes its fullscreen window fit the screen
                                  but it is not USSize'd or USPosition'd) */
                               !client_is_oldfullscreen(self, &place))));
    }

    /* if the window isn't user-sized, then make it fit inside
       the visible screen area on its monitor. Use basically the same rules
       for forcing the window on screen in the client_find_onscreen call.

       do this after place_client, it chooses the monitor!

       splash screens get "transient" set to TRUE by
       the place_client call
    */
    if (ob_state() == OB_STATE_RUNNING &&
        (transient ||
         (!(self->sized & USSize || self->positioned & USPosition) &&
          client_normal(self) &&
          /* don't shrink oldschool fullscreen windows to fit inside the
             struts (fixes Acroread, which makes its fullscreen window
             fit the screen but it is not USSize'd or USPosition'd) */
          !client_is_oldfullscreen(self, &place))))
    {
        Rect *a = screen_area(self->desktop, SCREEN_AREA_ONE_MONITOR, &place);

        /* get the size of the frame */
        place.width += self->frame->size.left + self->frame->size.right;
        place.height += self->frame->size.top + self->frame->size.bottom;

        /* fit the window inside the area */
        place.width = MIN(place.width, a->width);
        place.height = MIN(place.height, a->height);

        ob_debug("setting window size to %dx%d", place.width, place.height);

        /* get the size of the client back */
        place.width -= self->frame->size.left + self->frame->size.right;
        place.height -= self->frame->size.top + self->frame->size.bottom;

        g_slice_free(Rect, a);
    }

    ob_debug("placing window %s at %d, %d with size %d x %d. "
             "some restrictions may apply",
             self->title, place.x, place.y, place.width, place.height);

    /* do this after the window is placed, so the premax/prefullscreen numbers
       won't be all wacko!!

       this also places the window
    */
    client_apply_startup_state(self, place.x, place.y,
                               place.width, place.height);

    /* grab mouse bindings before showing the window */
    mouse_grab_for_client(self, TRUE);

    if (!config_focus_under_mouse)
        ignore_start = event_start_ignore_all_enters();

    /* add to client list/map */
    client_list = g_list_append(client_list, self);

    /* announce the window to taskbars and pagers */
    toplevel_new(self);

    /* this has to happen before we try focus the window, but we want it to
       happen after the client's stacking has been determined or it looks bad
    */
    client_show(self);

    /* activate/hilight/raise the window */
    if (try_activate) {
        if (do_activate)
            client_present(self, FALSE, TRUE, TRUE);
        else
            /* if the client isn't stealing focus, then hilite it so the user
               knows it is there */
            client_hilite(self, TRUE);
    }
    else {
        /* This may look rather odd. Well it's because new windows are added
           to the stacking order non-intrusively. If we're not going to focus
           the new window or hilite it, then we raise it to the top. This will
           take affect for things that don't get focused like splash screens.
           Also if you don't have focus_new enabled, then it's going to get
           raised to the top. Legacy begets legacy I guess?
        */
        stacking_raise(CLIENT_AS_WINDOW(self));
    }

    if (!config_focus_under_mouse)
        event_end_ignore_all_enters(ignore_start);

    /* this has to happen after we're in the client_list */
    if (STRUT_EXISTS(self->strut))
        screen_update_areas();

    /* update the list hints */
    client_set_list();

    /* free the ObAppSettings shallow copy */
    g_slice_free(ObAppSettings, settings);

    ob_debug("Managed window %s (%s)", self->title, self->class);
}

void client_unmanage_all(void)
{
    while (client_list)
        client_unmanage(client_list->data);
}

void client_unmanage(ObClient *self)
{
    GSList *it;
    gulong ignore_start;
    gboolean was_focused;

    ob_debug("Unmanaging window: %s (%s)",
             self->class, self->title ? self->title : "");

    g_assert(self != NULL);
    g_assert(self->managed);

    /* ignore enter events from the unmap so it doesnt mess with the focus */
    if (!config_focus_under_mouse)
        ignore_start = event_start_ignore_all_enters();

    frame_hide(self->frame);

    if (!config_focus_under_mouse)
        event_end_ignore_all_enters(ignore_start);

    mouse_grab_for_client(self, FALSE);

    self->managed = FALSE;
    seat_client_gone(self);

    if (self->configure_timeout) {
        g_source_remove(self->configure_timeout);
        self->configure_timeout = 0;
    }
    self->configure_pending = self->configure_queued = FALSE;

    /* update the focus lists */
    focus_order_remove(self);
    was_focused = client_focused(self);
    if (was_focused) {
        /* don't leave an invalid focus_client */
        focus_client = NULL;
    }

    /* if we're prompting to kill the client, close that */
    prompt_unref(self->kill_prompt);
    self->kill_prompt = NULL;

    client_list = g_list_remove(client_list, self);
    stacking_remove(self);

    /* once the client is out of the list, update the struts to remove its
       influence */
    if (STRUT_EXISTS(self->strut))
        screen_update_areas();

    client_call_notifies(self, client_destroy_notifies);

    toplevel_destroy(self);

    /* tell our parent(s) that we're gone */
    for (it = self->parents; it; it = g_slist_next(it))
        ((ObClient*)it->data)->transients =
            g_slist_remove(((ObClient*)it->data)->transients,self);

    /* tell our transients that we're gone */
    for (it = self->transients; it; it = g_slist_next(it)) {
        ((ObClient*)it->data)->parents =
            g_slist_remove(((ObClient*)it->data)->parents, self);
        /* we could be keeping our children in a higher layer */
        client_calc_layer(it->data);
    }

    /* take the surfaces out of the frame, and free the frame */
    frame_release_client(self->frame);
    frame_free(self->frame);
    self->frame = NULL;

    /* update the list hints */
    client_set_list();

    ob_debug("Unmanaged window %s", self->title);

    /* free all data allocated in the client struct, and get it ready to be
       managed again if the shell surface maps again */
    RrImageUnref(self->icon_set);
    g_slist_free(self->transients);
    g_slist_free(self->parents);
    g_free(self->startup_id);
    g_free(self->title);
    g_free(self->icon_title);
    g_free(self->original_title);
    g_free(self->name);
    g_free(self->class);
    g_free(self->role);
    g_free(self->group_name);
    g_free(self->group_class);
    g_free(self->client_machine);
    {
        const ObClientImpl *impl = self->impl;
        gpointer impl_data = self->impl_data;
        struct wlr_scene_tree *tree = self->surface_tree;
        Size committed = self->committed_size;
        gint gx = self->geometry_x, gy = self->geometry_y;

        memset(self, 0, sizeof(ObClient));
        self->obwin.type = OB_WINDOW_CLASS_CLIENT;
        self->node.type = OB_NODE_WINDOW;
        self->node.owner = CLIENT_AS_WINDOW(self);
        self->impl = impl;
        self->impl_data = impl_data;
        self->surface_tree = tree;
        self->committed_size = committed;
        self->geometry_x = gx;
        self->geometry_y = gy;
    }

    /* the focused window is gone, pick something else */
    if (was_focused && ob_state() == OB_STATE_RUNNING)
        focus_fallback(FALSE, config_focus_under_mouse, TRUE, TRUE);
}

static gboolean client_can_steal_focus(ObClient *self,
                                       gboolean allow_other_desktop,
                                       gboolean request_from_user,
                                       guint32 steal_time,
                                       guint32 launch_time)
{
    gboolean steal;
    gboolean relative_focused;

    steal = TRUE;

    relative_focused = (focus_client != NULL &&
                        (client_search_focus_tree_full(self) != NULL ||
                         client_search_focus_group_full(self) != NULL));

    /* This is focus stealing prevention */
    ob_debug("Want to focus window %s at time %u "
             "launched at %u (last user interaction time %u) "
             "request from %s, allow other desktop: %s, "
             "desktop switch time %u",
             self->title, steal_time, launch_time,
             event_last_user_time,
             (request_from_user ? "user" : "other"),
             (allow_other_desktop ? "yes" : "no"),
             screen_desktop_user_time);

    /*
      if no launch time is provided for an application, make one up.

      if the window is related to other existing windows
        and one of those windows was the last used
          then we will give it a launch time equal to the last user time,
          which will end up giving the window focus probably.
        else
          the window is related to other windows, but you are not working in
          them?
          seems suspicious, so we will give it a launch time of
          NOW - STEAL_INTERVAL,
          so it will be given focus only if we didn't use something else
          during the steal interval.
      else
        the window is all on its own, so we can't judge it.  give it a launch
        time equal to the last user time, so it will probably take focus.

      this way running things from a terminal will give them focus, but popups
      without a launch time shouldn't steal focus so easily.
    */

    if (!launch_time) {
        if (client_has_relative(self)) {
            if (event_last_user_time && client_search_focus_group_full(self)) {
                /* our relative is focused */
                launch_time = event_last_user_time;
                ob_debug("Unknown launch time, using %u - window in active "
                         "group", launch_time);
            }
            else if (!request_from_user) {
                /* has relatives which are not being used. suspicious */
                launch_time = event_time() - OB_EVENT_USER_TIME_DELAY;
                ob_debug("Unknown launch time, using %u - window in inactive "
                         "group", launch_time);
            }
            else {
                /* has relatives which are not being used, but the user seems
                   to want to go there! */
            launch_time = event_last_user_time;
            ob_debug("Unknown launch time, using %u - user request",
                     launch_time);
            }
        }
        else {
            /* the window is on its own, probably the user knows it is going
               to appear */
            launch_time = event_last_user_time;
            ob_debug("Unknown launch time, using %u - independent window",
                     launch_time);
        }
    }

    /* if it's on another desktop
       and if allow_other_desktop is true, we generally let it steal focus.
       but if it didn't come from the user, don't let it steal unless it was
       launched before the user switched desktops.
       focus, unless it was launched after we changed desktops and the request
       came from the user
     */
    if (!screen_compare_desktops(screen_desktop, self->desktop)) {
        /* must be allowed */
        if (!allow_other_desktop) {
            steal = FALSE;
            ob_debug("Not focusing the window because its on another desktop");
        }
        /* if we don't know when the desktop changed, but request is from an
           application, don't let it change desktop on you */
        else if (!request_from_user) {
            steal = FALSE;
            ob_debug("Not focusing the window because non-user request");
        }
    }
    /* If something is focused... */
    else if (focus_client) {
        /* If the user is working in another window right now, then don't
           steal focus */
        if (!relative_focused &&
            event_last_user_time &&
            /* last user time must be strictly > launch_time to block focus */
            (event_time_after(event_last_user_time, launch_time) &&
             event_last_user_time != launch_time) &&
            event_time_after(event_last_user_time,
                             steal_time - OB_EVENT_USER_TIME_DELAY))
        {
            steal = FALSE;
            ob_debug("Not focusing the window because the user is "
                     "working in another window that is not its relative");
        }
        /* Don't move focus if it's not going to go to this window
           anyway */
        else if (client_focus_target(self) != self) {
            steal = FALSE;
            ob_debug("Not focusing the window because another window "
                     "would get the focus anyway");
        }
        /* For requests that don't come from the user */
        else if (!request_from_user) {
            /* If the new window is a transient (and its relatives aren't
               focused) */
            if (client_has_parent(self) && !relative_focused) {
                steal = FALSE;
                ob_debug("Not focusing the window because it is a "
                         "transient, and its relatives aren't focused");
            }
            /* Don't steal focus from globally active clients.
               I stole this idea from KWin. It seems nice.
            */
            else if (!focus_client->can_focus)
            {
                steal = FALSE;
                ob_debug("Not focusing the window because a globally "
                         "active client has focus");
            }
            /* Don't move focus if the window is not visible on the current
               desktop and none of its relatives are focused */
            else if (!allow_other_desktop &&
                     !screen_compare_desktops(self->desktop, screen_desktop) &&
                     !relative_focused)
            {
                steal = FALSE;
                ob_debug("Not focusing the window because it is on "
                         "another desktop and no relatives are focused ");
            }
        }
    }

    if (!steal)
        ob_debug("Focus stealing prevention activated for %s at "
                 "time %u (last user interaction time %u)",
                 self->title, steal_time, event_last_user_time);
    else
        ob_debug("Allowing focus stealing for %s at time %u (last user "
                 "interaction time %u)",
                 self->title, steal_time, event_last_user_time);
    return steal;
}

/*! Returns a new structure containing the per-app settings for this client.
  The returned structure needs to be freed with g_free. */
static ObAppSettings *client_get_settings_state(ObClient *self)
{
    ObAppSettings *settings;
    GSList *it;

    settings = config_create_app_settings();

    for (it = config_per_app_settings; it; it = g_slist_next(it)) {
        ObAppSettings *app = it->data;
        gboolean match = TRUE;

        g_assert(app->name != NULL || app->class != NULL ||
                 app->role != NULL || app->title != NULL ||
                 app->group_name != NULL || app->group_class != NULL ||
                 (signed)app->type >= 0);

        if (app->name &&
            !g_pattern_match(app->name, strlen(self->name), self->name, NULL))
            match = FALSE;
        else if (app->group_name &&
            !g_pattern_match(app->group_name,
                             strlen(self->group_name), self->group_name, NULL))
            match = FALSE;
        else if (app->class &&
                 !g_pattern_match(app->class,
                                  strlen(self->class), self->class, NULL))
            match = FALSE;
        else if (app->group_class &&
                 !g_pattern_match(app->group_class,
                                  strlen(self->group_class), self->group_class,
                                  NULL))
            match = FALSE;
        else if (app->role &&
                 !g_pattern_match(app->role,
                                  strlen(self->role), self->role, NULL))
            match = FALSE;
        else if (app->title &&
                 !g_pattern_match(app->title,
                                  strlen(self->title), self->title, NULL))
            match = FALSE;
        else if ((signed)app->type >= 0 && app->type != self->type) {
            match = FALSE;
        }

        if (match) {
            ob_debug("Window matching: %s", app->name);

            /* copy the settings to our struct, overriding the existing
               settings if they are not defaults */
            config_app_settings_copy_non_defaults(app, settings);
        }
    }

    if (settings->shade != -1)
        self->shaded = !!settings->shade;
    if (settings->decor != -1)
        self->undecorated = !settings->decor;
    if (settings->iconic != -1)
        self->iconic = !!settings->iconic;
    if (settings->skip_pager != -1)
        self->skip_pager = !!settings->skip_pager;
    if (settings->skip_taskbar != -1)
        self->skip_taskbar = !!settings->skip_taskbar;

    if (settings->max_vert != -1)
        self->max_vert = !!settings->max_vert;
    if (settings->max_horz != -1)
        self->max_horz = !!settings->max_horz;

    if (settings->fullscreen != -1)
        self->fullscreen = !!settings->fullscreen;

    if (settings->desktop) {
        if (settings->desktop == DESKTOP_ALL)
            self->desktop = settings->desktop;
        else if (settings->desktop > 0 &&
                 settings->desktop <= screen_num_desktops)
            self->desktop = settings->desktop - 1;
    }

    if (settings->layer == -1) {
        self->below = TRUE;
        self->above = FALSE;
    }
    else if (settings->layer == 0) {
        self->below = FALSE;
        self->above = FALSE;
    }
    else if (settings->layer == 1) {
        self->below = FALSE;
        self->above = TRUE;
    }
    return settings;
}

void client_move_onscreen(ObClient *self, gboolean rude)
{
    gint x = self->area.x;
    gint y = self->area.y;
    if (client_find_onscreen(self, &x, &y,
                             self->area.width,
                             self->area.height, rude)) {
        client_move(self, x, y);
    }
}

gboolean client_find_onscreen(ObClient *self, gint *x, gint *y, gint w, gint h,
                              gboolean rude)
{
    gint ox = *x, oy = *y;
    gboolean rudel = rude, ruder = rude, rudet = rude, rudeb = rude;
    gint fw, fh;
    Rect desired;
    guint i;
    gboolean found_mon;

    RECT_SET(desired, *x, *y, w, h);
    frame_rect_to_frame(self->frame, &desired);

    /* get where the frame would be */
    frame_client_gravity(self->frame, x, y);

    /* get the requested size of the window with decorations */
    fw = self->frame->size.left + w + self->frame->size.right;
    fh = self->frame->size.top + h + self->frame->size.bottom;

    /* If rudeness wasn't requested, then still be rude in a given direction
       if the client is not moving, only resizing in that direction */
    if (!rude) {
        Point oldtl, oldtr, oldbl, oldbr;
        Point newtl, newtr, newbl, newbr;
        gboolean stationary_l, stationary_r, stationary_t, stationary_b;

        POINT_SET(oldtl, self->frame->area.x, self->frame->area.y);
        POINT_SET(oldbr, self->frame->area.x + self->frame->area.width - 1,
                  self->frame->area.y + self->frame->area.height - 1);
        POINT_SET(oldtr, oldbr.x, oldtl.y);
        POINT_SET(oldbl, oldtl.x, oldbr.y);

        POINT_SET(newtl, *x, *y);
        POINT_SET(newbr, *x + fw - 1, *y + fh - 1);
        POINT_SET(newtr, newbr.x, newtl.y);
        POINT_SET(newbl, newtl.x, newbr.y);

        /* is it moving or just resizing from some corner? */
        stationary_l = oldtl.x == newtl.x;
        stationary_r = oldtr.x == newtr.x;
        stationary_t = oldtl.y == newtl.y;
        stationary_b = oldbl.y == newbl.y;

        /* if left edge is growing and didnt move right edge */
        if (stationary_r && newtl.x < oldtl.x)
            rudel = TRUE;
        /* if right edge is growing and didnt move left edge */
        if (stationary_l && newtr.x > oldtr.x)
            ruder = TRUE;
        /* if top edge is growing and didnt move bottom edge */
        if (stationary_b && newtl.y < oldtl.y)
            rudet = TRUE;
        /* if bottom edge is growing and didnt move top edge */
        if (stationary_t && newbl.y > oldbl.y)
            rudeb = TRUE;
    }

    /* we iterate through every monitor that the window is at least partially
       on, to make sure it is obeying the rules on them all

       if the window does not appear on any monitors, then use the first one
    */
    found_mon = FALSE;
    for (i = 0; i < screen_num_monitors; ++i) {
        Rect *a;

        if (!screen_physical_area_monitor_contains(i, &desired)) {
            if (i < screen_num_monitors - 1 || found_mon)
                continue;

            /* the window is not inside any monitor! so just use the first
               one */
            a = screen_area(self->desktop, 0, NULL);
        } else {
            found_mon = TRUE;
            a = screen_area(self->desktop, SCREEN_AREA_ONE_MONITOR, &desired);
        }

        /* This makes sure windows aren't entirely outside of the screen so you
           can't see them at all.
           It makes sure 10% of the window is on the screen at least. And don't
           let it move itself off the top of the screen, which would hide the
           titlebar on you. (The user can still do this if they want too, it's
           only limiting the application.
        */
        if (client_normal(self)) {
            if (!self->strut.right && *x + fw/10 >= a->x + a->width - 1)
                *x = a->x + a->width - fw/10;
            if (!self->strut.bottom && *y + fh/10 >= a->y + a->height - 1)
                *y = a->y + a->height - fh/10;
            if (!self->strut.left && *x + fw*9/10 - 1 < a->x)
                *x = a->x - fw*9/10;
            if (!self->strut.top && *y + fh*9/10 - 1 < a->y)
                *y = a->y - fh*9/10;
        }

        /* This here doesn't let windows even a pixel outside the
           struts/screen. When called from client_manage, programs placing
           themselves are forced completely onscreen, while things like
           xterm -geometry resolution-width/2 will work fine. Trying to
           place it completely offscreen will be handled in the above code.
           Sorry for this confused comment, i am tired. */
        if (rudel && !self->strut.left && *x < a->x) *x = a->x;
        if (ruder && !self->strut.right && *x + fw > a->x + a->width)
            *x = a->x + MAX(0, a->width - fw);

        if (rudet && !self->strut.top && *y < a->y) *y = a->y;
        if (rudeb && !self->strut.bottom && *y + fh > a->y + a->height)
            *y = a->y + MAX(0, a->height - fh);

        g_slice_free(Rect, a);
    }

    /* get where the client should be */
    frame_frame_gravity(self->frame, x, y);

    return ox != *x || oy != *y;
}

static void client_get_all(ObClient *self, gboolean real)
{
    /* this is needed for the frame to set itself up */
    client_get_area(self);

    /* these things can change the decor and functions of the window */

    /* this can change the mwmhints for special cases */
    client_get_type_and_transientness(self);
    client_update_normal_hints(self);

    /* set up the maximum possible decor/functions */
    client_setup_default_decor_and_functions(self);

    client_get_state(self);

    /* get the session related properties, these can change decorations
       from per-app settings */
    client_get_session_ids(self);

    /* get this early so we have it for debugging, also this can be used
     by app rule matching */
    client_update_title(self);

    /* now we got everything that can affect the decorations or app rule
       matching */
    if (!real)
        return;

    client_update_wmhints(self);
    client_update_transient_for(self);

    client_get_desktop(self);/* uses transient data/group/startup id if a
                                desktop is not specified */

    {
        /* a couple type-based defaults for new windows */

        /* this makes sure that these windows appear on all desktops */
        if (self->type == OB_CLIENT_TYPE_DESKTOP)
            self->desktop = DESKTOP_ALL;
    }

    client_update_strut(self);
    client_update_icons(self);
    client_update_icon_geometry(self);
}

static void client_get_area(ObClient *self)
{
    RECT_SET(self->area, 0, 0, MAX(self->committed_size.width, 1),
             MAX(self->committed_size.height, 1));
    self->impl->get_area(self);
    POINT_SET(self->root_pos, self->area.x, self->area.y);
    self->border_width = 0;

    ob_debug("client area: %d %d  %d %d", self->area.x, self->area.y,
             self->area.width, self->area.height);
}

static void client_get_desktop(ObClient *self)
{
    guint32 d = screen_num_desktops; /* an always-invalid value */

    if (self->impl->get_desktop && self->impl->get_desktop(self, &d)) {
        if (d >= screen_num_desktops && d != DESKTOP_ALL)
            self->desktop = screen_num_desktops - 1;
        else
            self->desktop = d;
        ob_debug("client requested desktop 0x%x", self->desktop);
    } else {
        GSList *it;
        gboolean first = TRUE;
        guint all = screen_num_desktops; /* not a valid value */

        /* if they are all on one desktop, then open it on the
           same desktop */
        for (it = self->parents; it; it = g_slist_next(it)) {
            ObClient *c = it->data;

            if (c->desktop == DESKTOP_ALL) continue;

            if (first) {
                all = c->desktop;
                first = FALSE;
            }
            else if (all != c->desktop)
                all = screen_num_desktops; /* make it invalid */
        }
        if (all != screen_num_desktops) {
            self->desktop = all;

            ob_debug("client desktop set from parents: 0x%x",
                     self->desktop);
        }
        /* try get from the startup-notification protocol */
        else if (toplevel_activation_desktop(self, &self->desktop)) {
            if (self->desktop >= screen_num_desktops &&
                self->desktop != DESKTOP_ALL)
                self->desktop = screen_num_desktops - 1;
            ob_debug("client desktop set from startup-notification: 0x%x",
                     self->desktop);
        }
        /* defaults to the current desktop */
        else {
            self->desktop = screen_desktop;
            ob_debug("client desktop set to the current desktop: %d",
                     self->desktop);
        }
    }
}

static void client_get_state(ObClient *self)
{
    self->impl->get_state(self);
}

void client_update_transient_for(ObClient *self)
{
    ObClient *target = NULL;

    if (self->impl->get_parent)
        target = self->impl->get_parent(self);
    /* can't be transient to itself! */
    if (target == self)
        target = NULL;

    client_update_transient_tree(self, client_direct_parent(self), target);
}

/* Openbox also has group transients (windows transient for their whole X
   window group).  Wayland has no window groups, so only direct parents are
   kept. */
static void client_update_transient_tree(ObClient *self,
                                         ObClient* oldparent,
                                         ObClient *newparent)
{
    GSList *it, *next;
    ObClient *c;

    /* No change has occured */
    if (oldparent == newparent) return;

    /** Remove the client from the transient tree **/

    for (it = self->parents; it; it = next) {
        next = g_slist_next(it);
        c = it->data;
        self->parents = g_slist_delete_link(self->parents, it);
        c->transients = g_slist_remove(c->transients, self);
    }

    /** Re-add the client to the transient tree **/

    /* If we are now transient for a single window we need to add ourselves to
       its children

       WARNING: Cyclical transient-ness is possible if two windows are
       transient for eachother.
    */
    if (newparent &&
        /* don't make ourself its child if it is already our child */
        !client_is_direct_child(self, newparent) &&
        client_normal(newparent))
    {
        newparent->transients = g_slist_prepend(newparent->transients, self);
        self->parents = g_slist_prepend(self->parents, newparent);
    }
}

void client_get_type_and_transientness(ObClient *self)
{
    self->type = -1;
    self->transient = FALSE;
    self->mwmhints.flags = 0; /* default to none */

    self->impl->get_type(self);

    if (self->type == (ObClientType) -1) {
        /*the window type hint was not set, which means we either classify
          ourself as a normal window or a dialog, depending on if we are a
          transient. */
        if (self->transient)
            self->type = OB_CLIENT_TYPE_DIALOG;
        else
            self->type = OB_CLIENT_TYPE_NORMAL;
    }

    /* then, based on our type, we can update our transientness.. */
    if (self->type == OB_CLIENT_TYPE_DIALOG ||
        self->type == OB_CLIENT_TYPE_TOOLBAR ||
        self->type == OB_CLIENT_TYPE_MENU ||
        self->type == OB_CLIENT_TYPE_UTILITY)
    {
        self->transient = TRUE;
    }
}

#ifdef SYNC
#endif

void client_update_normal_hints(ObClient *self)
{
    /* defaults */
    self->min_ratio = 0.0f;
    self->max_ratio = 0.0f;
    SIZE_SET(self->size_inc, 1, 1);
    SIZE_SET(self->base_size, -1, -1);
    SIZE_SET(self->min_size, 0, 0);
    SIZE_SET(self->max_size, G_MAXINT, G_MAXINT);

    self->impl->get_normal_hints(self);

    ob_debug("Normal hints: min size (%d %d) max size (%d %d)",
             self->min_size.width, self->min_size.height,
             self->max_size.width, self->max_size.height);
}

static void client_setup_default_decor_and_functions(ObClient *self)
{
    /* start with everything (cept fullscreen) */
    self->decorations =
        (OB_FRAME_DECOR_TITLEBAR |
         OB_FRAME_DECOR_HANDLE |
         OB_FRAME_DECOR_GRIPS |
         OB_FRAME_DECOR_BORDER |
         OB_FRAME_DECOR_ICON |
         OB_FRAME_DECOR_ALLDESKTOPS |
         OB_FRAME_DECOR_ICONIFY |
         OB_FRAME_DECOR_MAXIMIZE |
         OB_FRAME_DECOR_SHADE |
         OB_FRAME_DECOR_CLOSE);
    self->functions =
        (OB_CLIENT_FUNC_RESIZE |
         OB_CLIENT_FUNC_MOVE |
         OB_CLIENT_FUNC_ICONIFY |
         OB_CLIENT_FUNC_MAXIMIZE |
         OB_CLIENT_FUNC_SHADE |
         OB_CLIENT_FUNC_CLOSE |
         OB_CLIENT_FUNC_BELOW |
         OB_CLIENT_FUNC_ABOVE |
         OB_CLIENT_FUNC_UNDECORATE);

    if (!(self->min_size.width < self->max_size.width ||
          self->min_size.height < self->max_size.height))
        self->functions &= ~OB_CLIENT_FUNC_RESIZE;

    switch (self->type) {
    case OB_CLIENT_TYPE_NORMAL:
        /* normal windows retain all of the possible decorations and
           functionality, and can be fullscreen */
        self->functions |= OB_CLIENT_FUNC_FULLSCREEN;
        break;

    case OB_CLIENT_TYPE_DIALOG:
        /* sometimes apps make dialog windows fullscreen for some reason (for
           e.g. kpdf does this..) */
        self->functions |= OB_CLIENT_FUNC_FULLSCREEN;
        break;

    case OB_CLIENT_TYPE_UTILITY:
        /* these windows don't have anything added or removed by default */
        break;

    case OB_CLIENT_TYPE_MENU:
    case OB_CLIENT_TYPE_TOOLBAR:
        /* these windows can't iconify or maximize */
        self->decorations &= ~(OB_FRAME_DECOR_ICONIFY |
                               OB_FRAME_DECOR_MAXIMIZE);
        self->functions &= ~(OB_CLIENT_FUNC_ICONIFY |
                             OB_CLIENT_FUNC_MAXIMIZE);
        break;

    case OB_CLIENT_TYPE_SPLASH:
        /* these don't get get any decorations, and the only thing you can
           do with them is move them */
        self->decorations = 0;
        self->functions = OB_CLIENT_FUNC_MOVE;
        break;

    case OB_CLIENT_TYPE_DESKTOP:
        /* these windows are not manipulated by the window manager */
        self->decorations = 0;
        self->functions = 0;
        break;

    case OB_CLIENT_TYPE_DOCK:
        /* these windows are not manipulated by the window manager, but they
           can set below layer which has a special meaning */
        self->decorations = 0;
        self->functions = OB_CLIENT_FUNC_BELOW;
        break;
    }

    /* If the client has no decor from its type (which never changes) then
       don't allow the user to "undecorate" the window.  Otherwise, allow them
       to, even if there are motif hints removing the decor, because those
       may change these days (e.g. chromium) */
    if (self->decorations == 0)
        self->functions &= ~OB_CLIENT_FUNC_UNDECORATE;

    /* Mwm Hints are applied subtractively to what has already been chosen for
       decor and functionality */
    if (self->mwmhints.flags & OB_MWM_FLAG_DECORATIONS) {
        if (! (self->mwmhints.decorations & OB_MWM_DECOR_ALL)) {
            if (! ((self->mwmhints.decorations & OB_MWM_DECOR_HANDLE) ||
                   (self->mwmhints.decorations & OB_MWM_DECOR_TITLE)))
            {
                /* if the mwm hints request no handle or title, then all
                   decorations are disabled, but keep the border if that's
                   specified */
                if (self->mwmhints.decorations & OB_MWM_DECOR_BORDER)
                    self->decorations = OB_FRAME_DECOR_BORDER;
                else
                    self->decorations = 0;
            }
        }
    }

    if (self->mwmhints.flags & OB_MWM_FLAG_FUNCTIONS) {
        if (! (self->mwmhints.functions & OB_MWM_FUNC_ALL)) {
            if (! (self->mwmhints.functions & OB_MWM_FUNC_RESIZE))
                self->functions &= ~OB_CLIENT_FUNC_RESIZE;
            if (! (self->mwmhints.functions & OB_MWM_FUNC_MOVE))
                self->functions &= ~OB_CLIENT_FUNC_MOVE;
            /* dont let mwm hints kill any buttons
               if (! (self->mwmhints.functions & OB_MWM_FUNC_ICONIFY))
               self->functions &= ~OB_CLIENT_FUNC_ICONIFY;
               if (! (self->mwmhints.functions & OB_MWM_FUNC_MAXIMIZE))
               self->functions &= ~OB_CLIENT_FUNC_MAXIMIZE;
            */
            /* dont let mwm hints kill the close button
               if (! (self->mwmhints.functions & MwmFunc_Close))
               self->functions &= ~OB_CLIENT_FUNC_CLOSE; */
        }
    }

    if (!(self->functions & OB_CLIENT_FUNC_SHADE))
        self->decorations &= ~OB_FRAME_DECOR_SHADE;
    if (!(self->functions & OB_CLIENT_FUNC_ICONIFY))
        self->decorations &= ~OB_FRAME_DECOR_ICONIFY;
    if (!(self->functions & OB_CLIENT_FUNC_RESIZE))
        self->decorations &= ~(OB_FRAME_DECOR_GRIPS | OB_FRAME_DECOR_HANDLE);

    /* can't maximize without moving/resizing */
    if (!((self->functions & OB_CLIENT_FUNC_MAXIMIZE) &&
          (self->functions & OB_CLIENT_FUNC_MOVE) &&
          (self->functions & OB_CLIENT_FUNC_RESIZE))) {
        self->functions &= ~OB_CLIENT_FUNC_MAXIMIZE;
        self->decorations &= ~OB_FRAME_DECOR_MAXIMIZE;
    }
}

/*! Set up decor for a client based on its undecorated state. */
static void client_setup_decor_undecorated(ObClient *self)
{
    /* If the user requested no decorations, then remove all the decorations,
       except the border.  But don't add a border if there wasn't one. */
    if (self->undecorated)
        self->decorations &= (config_theme_keepborder ?
                              OB_FRAME_DECOR_BORDER : 0);
}

void client_setup_decor_and_functions(ObClient *self, gboolean reconfig)
{
    client_setup_default_decor_and_functions(self);

    client_setup_decor_undecorated(self);

    if (self->max_horz && self->max_vert) {
        /* once upon a time you couldn't resize maximized windows, that is not
           the case any more though !

           but do kill the handle on fully maxed windows */
        self->decorations &= ~(OB_FRAME_DECOR_HANDLE | OB_FRAME_DECOR_GRIPS);
    }

    /* if we don't have a titlebar, then we cannot shade! */
    if (!(self->decorations & OB_FRAME_DECOR_TITLEBAR))
        self->functions &= ~OB_CLIENT_FUNC_SHADE;

    /* now we need to check against rules for the client's current state */
    if (self->fullscreen) {
        self->functions &= (OB_CLIENT_FUNC_CLOSE |
                            OB_CLIENT_FUNC_MOVE |
                            OB_CLIENT_FUNC_FULLSCREEN |
                            OB_CLIENT_FUNC_ICONIFY);
        self->decorations = 0;
    }

    client_change_allowed_actions(self);

    if (reconfig)
        /* reconfigure to make sure decorations are updated */
        client_reconfigure(self, FALSE);
}

static void client_change_allowed_actions(ObClient *self)
{
    /* make sure the window isn't breaking any rules now

       don't check ICONIFY here.  just cuz a window can't iconify doesnt mean
       it can't be iconified with its parent
    */

    if (!(self->functions & OB_CLIENT_FUNC_SHADE) && self->shaded) {
        if (self->frame) client_shade(self, FALSE);
        else self->shaded = FALSE;
    }
    if (!(self->functions & OB_CLIENT_FUNC_FULLSCREEN) && self->fullscreen) {
        if (self->frame) client_fullscreen(self, FALSE);
        else self->fullscreen = FALSE;
    }
    if (!(self->functions & OB_CLIENT_FUNC_MAXIMIZE) && (self->max_horz ||
                                                         self->max_vert)) {
        if (self->frame) client_maximize(self, FALSE, 0);
        else self->max_vert = self->max_horz = FALSE;
    }
}

void client_update_wmhints(ObClient *self)
{
    /* assume a window takes input if it doesn't specify */
    self->can_focus = TRUE;

    focus_cycle_addremove(self, TRUE);
}

void client_update_title(ObClient *self)
{
    gchar *data = NULL;
    gchar *visible = NULL;

    g_free(self->original_title);

    data = self->impl->get_title(self);
    if (!data) {
        if (self->transient) {
   /*
   GNOME alert windows are not given titles:
   http://developer.gnome.org/projects/gup/hig/draft_hig_new/windows-alert.html
   */
            data = g_strdup("");
        } else {
            if (self->class && *self->class)
                data = g_strdup(self->class);
            else if (self->name && *self->name)
                data = g_strdup(self->name);
            else
                data = g_strdup(_("Unnamed Window"));
        }
    }
    self->original_title = g_strdup(data);

    if (self->client_machine) {
        visible = g_strdup_printf("%s (%s)", data, self->client_machine);
        g_free(data);
    } else
        visible = data;

    if (self->not_responding) {
        data = visible;
        if (self->kill_level > 0)
            visible = g_strdup_printf("%s - [%s]", data, _("Killing..."));
        else
            visible = g_strdup_printf("%s - [%s]", data, _("Not Responding"));
        g_free(data);
    }

    if (!self->title || strcmp(self->title, visible)) {
        g_free(self->title);
        self->title = visible;
        if (self->frame)
            toplevel_update_title(self);
    } else
        g_free(visible);

    if (self->frame)
        frame_adjust_title(self->frame);

    /* update the icon title, Wayland has no separate icon title */
    g_free(self->icon_title);
    self->icon_title = g_strdup(self->title);
}

void client_update_strut(ObClient *self)
{
    StrutPartial strut;

    STRUT_PARTIAL_SET(strut, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0);
    if (self->impl->get_strut)
        self->impl->get_strut(self, &strut);

    if (!PARTIAL_STRUT_EQUAL(strut, self->strut)) {
        self->strut = strut;

        /* updating here is pointless while we're being mapped cuz we're not in
           the client list yet */
        if (self->frame)
            screen_update_areas();
    }
}

void client_update_icons(ObClient *self)
{
    RrImage *img = NULL;

    if (self->impl->get_icon)
        img = self->impl->get_icon(self);

    /* set the client's icons to be whatever we found */
    RrImageUnref(self->icon_set);
    self->icon_set = img;

    if (self->frame)
        frame_adjust_icon(self->frame);
}

void client_update_icon_geometry(ObClient *self)
{
    /* taskbars tell us where the window's button is through the foreign
       toplevel protocol, see toplevel.c */
}

static void client_get_session_ids(ObClient *self)
{
    self->impl->get_class(self);

    /* app rules match against these, so they are never NULL */
    if (!self->name) self->name = g_strdup("");
    if (!self->class) self->class = g_strdup("");
    if (!self->role) self->role = g_strdup("");
    if (!self->group_name) self->group_name = g_strdup(self->name);
    if (!self->group_class) self->group_class = g_strdup(self->class);
}

const gchar *client_type_to_string(ObClient *self)
{
    const gchar *type;

    switch (self->type) {
    case OB_CLIENT_TYPE_NORMAL:
        type = "normal"; break;
    case OB_CLIENT_TYPE_DIALOG:
        type = "dialog"; break;
    case OB_CLIENT_TYPE_UTILITY:
        type = "utility"; break;
    case OB_CLIENT_TYPE_MENU:
        type = "menu"; break;
    case OB_CLIENT_TYPE_TOOLBAR:
        type = "toolbar"; break;
    case OB_CLIENT_TYPE_SPLASH:
        type = "splash"; break;
    case OB_CLIENT_TYPE_DESKTOP:
        type = "desktop"; break;
    case OB_CLIENT_TYPE_DOCK:
        type = "dock"; break;
    }

    return type;
}

/*! Save the properties used for app matching rules, as seen by Openbox when
  the window mapped, so that users can still access them later if the app
  changes them */
static void client_change_wm_state(ObClient *self)
{
    if (self->shaded || self->iconic ||
        (self->desktop != DESKTOP_ALL && self->desktop != screen_desktop))
    {
        self->wmstate = IconicState;
    } else
        self->wmstate = NormalState;
}

static void client_change_state(ObClient *self)
{
    if (self->impl->change_state)
        self->impl->change_state(self);
    toplevel_update_state(self);

    if (self->frame)
        frame_adjust_state(self->frame);
}

ObClient *client_search_focus_tree(ObClient *self)
{
    GSList *it;
    ObClient *ret;

    for (it = self->transients; it; it = g_slist_next(it)) {
        if (client_focused(it->data)) return it->data;
        if ((ret = client_search_focus_tree(it->data))) return ret;
    }
    return NULL;
}

ObClient *client_search_focus_tree_full(ObClient *self)
{
    if (self->parents) {
        GSList *it;

        for (it = self->parents; it; it = g_slist_next(it)) {
            ObClient *c = it->data;
            if ((c = client_search_focus_tree_full(c))) return c;
        }

        return NULL;
    }
    else {
        /* this function checks the whole tree, the client_search_focus_tree
           does not, so we need to check this window */
        if (client_focused(self))
            return self;
        return client_search_focus_tree(self);
    }
}

ObClient *client_search_focus_group_full(ObClient *self)
{
    /* no window groups in Wayland */
    if (client_focused(self)) return self;
    return NULL;
}

gboolean client_has_parent(ObClient *self)
{
    return self->parents != NULL;
}

gboolean client_has_children(ObClient *self)
{
    return self->transients != NULL;
}

gboolean client_is_oldfullscreen(const ObClient *self,
                                 const Rect *area)
{
    const Rect *monitor, *allmonitors;

    /* No decorations and fills the monitor = oldskool fullscreen.
       But not for maximized windows.
    */

    if (self->decorations || self->max_horz || self->max_vert) return FALSE;

    monitor = screen_physical_area_monitor(screen_find_monitor(area));
    allmonitors = screen_physical_area_all_monitors();

    return (RECT_EQUAL(*area, *monitor) ||
            RECT_EQUAL(*area, *allmonitors));
}

static ObStackingLayer calc_layer(ObClient *self)
{
    ObStackingLayer l;

    if (self->type == OB_CLIENT_TYPE_DESKTOP)
        l = OB_STACKING_LAYER_DESKTOP;
    else if (self->type == OB_CLIENT_TYPE_DOCK) {
        if (self->below) l = OB_STACKING_LAYER_NORMAL;
        else l = OB_STACKING_LAYER_ABOVE;
    }
    else if ((self->fullscreen ||
              client_is_oldfullscreen(self, &self->area)) &&
             /* you are fullscreen while you or your children are focused.. */
             (client_focused(self) || client_search_focus_tree(self) ||
              /* you can be fullscreen if you're on another desktop */
              (self->desktop != screen_desktop &&
               self->desktop != DESKTOP_ALL) ||
              /* and you can also be fullscreen if the focused client is on
                 another monitor, or nothing else is focused */
              (!focus_client ||
               client_monitor(focus_client) != client_monitor(self))))
        l = OB_STACKING_LAYER_FULLSCREEN;
    else if (self->above) l = OB_STACKING_LAYER_ABOVE;
    else if (self->below) l = OB_STACKING_LAYER_BELOW;
    else l = OB_STACKING_LAYER_NORMAL;

    return l;
}

static void client_calc_layer_recursive(ObClient *self, ObClient *orig,
                                        ObStackingLayer min)
{
    ObStackingLayer old, own;
    GSList *it;

    old = self->layer;
    own = calc_layer(self);
    self->layer = MAX(own, min);

    if (self->layer != old) {
        stacking_remove(CLIENT_AS_WINDOW(self));
        stacking_add_nonintrusive(CLIENT_AS_WINDOW(self));
    }

    /* we've been restacked */
    self->visited = TRUE;

    for (it = self->transients; it; it = g_slist_next(it))
        client_calc_layer_recursive(it->data, orig,
                                    self->layer);
}

static void client_calc_layer_internal(ObClient *self)
{
    GSList *parents, *sit;

    /* transients take on the layer of their parents */
    parents = client_search_all_top_parents(self);

    for (sit = parents; sit; sit = g_slist_next(sit))
        client_calc_layer_recursive(sit->data, self, 0);
    g_slist_free(parents);
}

void client_calc_layer(ObClient *self)
{
    GList *it, *fs_start, *fs_end;
    /* the client_calc_layer_internal calls below modify stacking_list,
       so we have to make a copy to iterate over */
    GList *list = g_list_copy(stacking_list);

    /* skip over stuff above fullscreen layer */
    for (it = list; it; it = g_list_next(it))
        if (window_layer(it->data) <= OB_STACKING_LAYER_FULLSCREEN) break;

    fs_start = it;

    /* find the windows in the fullscreen layer, and mark them not-visited */
    for (; it; it = g_list_next(it)) {
        if (window_layer(it->data) < OB_STACKING_LAYER_FULLSCREEN) break;
        else if (WINDOW_IS_CLIENT(it->data))
            WINDOW_AS_CLIENT(it->data)->visited = FALSE;
    }

    fs_end = it;

    client_calc_layer_internal(self);

    /* now recalc any windows that were in the fullscreen layer which have not
       had their layer recalced already */
    for (it = fs_start; it != fs_end; it = g_list_next(it))
        if (WINDOW_IS_CLIENT(it->data) &&
                !WINDOW_AS_CLIENT(it->data)->visited)
            client_calc_layer_internal(it->data);

    g_list_free(list);
}

gboolean client_should_show(ObClient *self)
{
    if (self->iconic)
        return FALSE;
    if (client_normal(self) && screen_showing_desktop())
        return FALSE;
    if (self->desktop == screen_desktop || self->desktop == DESKTOP_ALL)
        return TRUE;

    return FALSE;
}

gboolean client_show(ObClient *self)
{
    gboolean show = FALSE;

    if (client_should_show(self)) {
        /* replay pending pointer event before showing the window, in case it
           should be going to something under the window */
        mouse_replay_pointer();

        frame_show(self->frame);
        show = TRUE;

        /* According to the ICCCM (sec 4.1.3.1) when a window is not visible,
           it needs to be in IconicState. This includes when it is on another
           desktop!
        */
        client_change_wm_state(self);
    }
    return show;
}

gboolean client_hide(ObClient *self)
{
    gboolean hide = FALSE;

    if (!client_should_show(self)) {
        /* We don't need to ignore enter events here.
           The window can hide/iconify in 3 different ways:
           1 - through an x message. in this case we ignore all enter events
               caused by responding to the x message (unless underMouse)
           2 - by a keyboard action. in this case we ignore all enter events
               caused by the action
           3 - by a mouse action. in this case they are doing stuff with the
               mouse and focus _should_ move.

           Also in action_end, we simulate an enter event that can't be ignored
           so trying to ignore them is futile in case 3 anyways
        */

        /* replay pending pointer event before hiding the window, in case it
           should be going to the window */
        mouse_replay_pointer();

        frame_hide(self->frame);
        hide = TRUE;

        /* According to the ICCCM (sec 4.1.3.1) when a window is not visible,
           it needs to be in IconicState. This includes when it is on another
           desktop!
        */
        client_change_wm_state(self);
    }
    return hide;
}

void client_showhide(ObClient *self)
{
    if (!client_show(self))
        client_hide(self);
}

gboolean client_normal(ObClient *self) {
    return ! (self->type == OB_CLIENT_TYPE_DESKTOP ||
              self->type == OB_CLIENT_TYPE_DOCK ||
              self->type == OB_CLIENT_TYPE_SPLASH);
}

gboolean client_helper(ObClient *self)
{
    return (self->type == OB_CLIENT_TYPE_UTILITY ||
            self->type == OB_CLIENT_TYPE_MENU ||
            self->type == OB_CLIENT_TYPE_TOOLBAR);
}

gboolean client_occupies_space(ObClient *self)
{
    return !(self->type == OB_CLIENT_TYPE_DESKTOP ||
             self->type == OB_CLIENT_TYPE_SPLASH);
}

gboolean client_mouse_focusable(ObClient *self)
{
    return !(self->type == OB_CLIENT_TYPE_MENU ||
             self->type == OB_CLIENT_TYPE_TOOLBAR ||
             self->type == OB_CLIENT_TYPE_SPLASH ||
             self->type == OB_CLIENT_TYPE_DOCK);
}

gboolean client_enter_focusable(ObClient *self)
{
    /* you can focus desktops but it shouldn't on enter */
    return (client_mouse_focusable(self) &&
            self->type != OB_CLIENT_TYPE_DESKTOP);
}

static void client_apply_startup_state(ObClient *self,
                                       gint x, gint y, gint w, gint h)
{
    /* save the states that we are going to apply */
    gboolean iconic = self->iconic;
    gboolean fullscreen = self->fullscreen;
    gboolean undecorated = self->undecorated;
    gboolean shaded = self->shaded;
    gboolean demands_attention = self->demands_attention;
    gboolean max_horz = self->max_horz;
    gboolean max_vert = self->max_vert;
    Rect oldarea;
    gint l;

    /* turn them all off in the client, so they won't affect the window
       being placed */
    self->iconic = self->fullscreen = self->undecorated = self->shaded =
        self->demands_attention = self->max_horz = self->max_vert = FALSE;

    /* move the client to its placed position, or it it's already there,
       generate a ConfigureNotify telling the client where it is.

       do this after adjusting the frame. otherwise it gets all weird and
       clients don't work right

       do this before applying the states so they have the correct
       pre-max/pre-fullscreen values
    */
    client_try_configure(self, &x, &y, &w, &h, &l, &l, FALSE);
    ob_debug("placed window %s at %d, %d with size %d x %d",
             self->title, x, y, w, h);
    /* save the area, and make it where it should be for the premax stuff */
    oldarea = self->area;
    RECT_SET(self->area, x, y, w, h);

    /* apply the states. these are in a carefully crafted order.. */

    if (iconic)
        client_iconify(self, TRUE, FALSE, TRUE);
    if (undecorated)
        client_set_undecorated(self, TRUE);
    if (shaded)
        client_shade(self, TRUE);
    if (demands_attention)
        client_hilite(self, TRUE);

    if (max_vert && max_horz)
        client_maximize(self, TRUE, 0);
    else if (max_vert)
        client_maximize(self, TRUE, 2);
    else if (max_horz)
        client_maximize(self, TRUE, 1);

    /* fullscreen removes the ability to apply other states */
    if (fullscreen)
        client_fullscreen(self, TRUE);

    /* make sure client_setup_decor_and_functions() is called at least once */
    client_setup_decor_and_functions(self, FALSE);

    /* if the window hasn't been configured yet, then do so now, in fact the
       x,y,w,h may _not_ be the same as the area rect, which can end up
       meaning that the client isn't properly moved/resized by the fullscreen
       function
       pho can cause this because it maps at size of the screen but not 0,0
       so openbox moves it on screen to 0,0 (thus x,y=0,0 and area.x,y don't).
       then fullscreen'ing makes it go to 0,0 which it thinks it already is at
       cuz thats where the pre-fullscreen will be. however the actual area is
       not, so this needs to be called even if we have fullscreened/maxed
    */
    self->area = oldarea;
    client_configure(self, x, y, w, h, FALSE, TRUE, FALSE);

    /* nothing to do for the other states:
       skip_taskbar
       skip_pager
       modal
       above
       below
    */
}

void client_gravity_resize_w(ObClient *self, gint *x, gint oldw, gint neww)
{
    /* these should be the current values. this is for when you're not moving,
       just resizing */
    g_assert(*x == self->area.x);
    g_assert(oldw == self->area.width);

    /* horizontal */
    switch (self->gravity) {
    default:
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
    case StaticGravity:
    case ForgetGravity:
        break;
    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
        *x -= (neww - oldw) / 2;
        break;
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
        *x -= neww - oldw;
        break;
    }
}

void client_gravity_resize_h(ObClient *self, gint *y, gint oldh, gint newh)
{
    /* these should be the current values. this is for when you're not moving,
       just resizing */
    g_assert(*y == self->area.y);
    g_assert(oldh == self->area.height);

    /* vertical */
    switch (self->gravity) {
    default:
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
    case StaticGravity:
    case ForgetGravity:
        break;
    case WestGravity:
    case CenterGravity:
    case EastGravity:
        *y -= (newh - oldh) / 2;
        break;
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
        *y -= newh - oldh;
        break;
    }
}

void client_try_configure(ObClient *self, gint *x, gint *y, gint *w, gint *h,
                          gint *logicalw, gint *logicalh,
                          gboolean user)
{
    Rect desired = {*x, *y, *w, *h};
    frame_rect_to_frame(self->frame, &desired);

    /* make the frame recalculate its dimensions n shit without changing
       anything visible for real, this way the constraints below can work with
       the updated frame dimensions. */
    frame_adjust_area(self->frame, FALSE, TRUE, TRUE);

    /* cap any X windows at the size of an unsigned short */
    *w = MIN(*w,
             (gint)G_MAXUSHORT
             - self->frame->size.left - self->frame->size.right);
    *h = MIN(*h,
             (gint)G_MAXUSHORT
             - self->frame->size.top - self->frame->size.bottom);

    /* gets the frame's position */
    frame_client_gravity(self->frame, x, y);

    /* these positions are frame positions, not client positions */

    /* set the size and position if fullscreen */
    if (self->fullscreen) {
        const Rect *a;
        guint i;

        i = screen_find_monitor(&desired);
        a = screen_physical_area_monitor(i);

        *x = a->x;
        *y = a->y;
        *w = a->width;
        *h = a->height;

        user = FALSE; /* ignore if the client can't be moved/resized when it
                         is fullscreening */
    } else if (self->max_horz || self->max_vert) {
        Rect *a;
        guint i;

        /* use all possible struts when maximizing to the full screen */
        i = screen_find_monitor(&desired);
        a = screen_area(self->desktop, i,
                        (self->max_horz && self->max_vert ? NULL : &desired));

        /* set the size and position if maximized */
        if (self->max_horz) {
            *x = a->x;
            *w = a->width - self->frame->size.left - self->frame->size.right;
        }
        if (self->max_vert) {
            *y = a->y;
            *h = a->height - self->frame->size.top - self->frame->size.bottom;
        }

        user = FALSE; /* ignore if the client can't be moved/resized when it
                         is maximizing */

        g_slice_free(Rect, a);
    }

    /* gets the client's position */
    frame_frame_gravity(self->frame, x, y);

    /* work within the preferred sizes given by the window, these may have
       changed rather than it's requested width and height, so always run
       through this code */
    {
        gint basew, baseh, minw, minh;
        gint incw, inch, maxw, maxh;
        gfloat minratio, maxratio;

        incw = self->size_inc.width;
        inch = self->size_inc.height;
        minratio = self->fullscreen || (self->max_horz && self->max_vert) ?
            0 : self->min_ratio;
        maxratio = self->fullscreen || (self->max_horz && self->max_vert) ?
            0 : self->max_ratio;

        /* base size is substituted with min size if not specified */
        if (self->base_size.width >= 0 || self->base_size.height >= 0) {
            basew = self->base_size.width;
            baseh = self->base_size.height;
        } else {
            basew = self->min_size.width;
            baseh = self->min_size.height;
        }
        /* min size is substituted with base size if not specified */
        if (self->min_size.width || self->min_size.height) {
            minw = self->min_size.width;
            minh = self->min_size.height;
        } else {
            minw = self->base_size.width;
            minh = self->base_size.height;
        }

        /* This comment is no longer true */
        /* if this is a user-requested resize, then check against min/max
           sizes */

        /* smaller than min size or bigger than max size? */
        if (*w > self->max_size.width) *w = self->max_size.width;
        if (*w < minw) *w = minw;
        if (*h > self->max_size.height) *h = self->max_size.height;
        if (*h < minh) *h = minh;

        *w -= basew;
        *h -= baseh;

        /* the sizes to used for maximized */
        maxw = *w;
        maxh = *h;

        /* keep to the increments */
        *w /= incw;
        *h /= inch;

        /* you cannot resize to nothing */
        if (basew + *w < 1) *w = 1 - basew;
        if (baseh + *h < 1) *h = 1 - baseh;

        /* save the logical size */
        *logicalw = incw > 1 ? *w : *w + basew;
        *logicalh = inch > 1 ? *h : *h + baseh;

        *w *= incw;
        *h *= inch;

        /* if maximized/fs then don't use the size increments */
        if (self->fullscreen || self->max_horz) *w = maxw;
        if (self->fullscreen || self->max_vert) *h = maxh;

        *w += basew;
        *h += baseh;

        /* adjust the height to match the width for the aspect ratios.
           for this, min size is not substituted for base size ever. */
        if (self->base_size.width >= 0 && self->base_size.height >= 0) {
            *w -= self->base_size.width;
            *h -= self->base_size.height;
        }

        if (minratio)
            if (*h * minratio > *w) {
                *h = (gint)(*w / minratio);

                /* you cannot resize to nothing */
                if (*h < 1) {
                    *h = 1;
                    *w = (gint)(*h * minratio);
                }
            }
        if (maxratio)
            if (*h * maxratio < *w) {
                *h = (gint)(*w / maxratio);

                /* you cannot resize to nothing */
                if (*h < 1) {
                    *h = 1;
                    *w = (gint)(*h * minratio);
                }
            }

        if (self->base_size.width >= 0 && self->base_size.height >= 0) {
            *w += self->base_size.width;
            *h += self->base_size.height;
        }
    }

    /* these override the above states! if you cant move you can't move! */
    if (user) {
        if (!(self->functions & OB_CLIENT_FUNC_MOVE)) {
            *x = self->area.x;
            *y = self->area.y;
        }
        if (!(self->functions & OB_CLIENT_FUNC_RESIZE)) {
            *w = self->area.width;
            *h = self->area.height;
        }
    }

    g_assert(*w > 0);
    g_assert(*h > 0);
}

static gboolean configure_timeout(gpointer data)
{
    ObClient *self = data;

    ob_debug("Client %s did not draw at its new size in time", self->title);
    self->configure_timeout = 0;
    if (self->configure_pending)
        client_committed(self, self->committed_size.width,
                         self->committed_size.height);
    return FALSE;
}

/* Finish a resize that was sent to the client, showing the frame at the
   size the client chose */
static void client_finish_configure(ObClient *self, gint w, gint h)
{
    Rect r = self->pending_area;
    Rect old = self->pending_old_area;

    self->configure_pending = FALSE;
    if (self->configure_timeout) {
        g_source_remove(self->configure_timeout);
        self->configure_timeout = 0;
    }

    /* the client may have chosen a different size than requested, then keep
       the edges that the request didn't move where they are */
    if (w > 0 && w != r.width) {
        if (r.x != old.x && r.x + r.width == old.x + old.width)
            r.x += r.width - w;
        r.width = w;
    }
    if (h > 0 && h != r.height) {
        if (r.y != old.y && r.y + r.height == old.y + old.height)
            r.y += r.height - h;
        r.height = h;
    }

    client_apply_configure(self, r.x, r.y, r.width, r.height,
                           self->pending_user, TRUE);

    if (self->configure_queued) {
        Rect q = self->queued_area;
        self->configure_queued = FALSE;
        client_configure(self, q.x, q.y, q.width, q.height,
                         self->queued_user, self->queued_final, FALSE);
    }
}

void client_configure(ObClient *self, gint x, gint y, gint w, gint h,
                      gboolean user, gboolean final, gboolean force_reply)
{
    gboolean resized;
    gboolean send_resize_client;
    gint logicalw, logicalh;

    /* only one resize can be in flight at a time, the newest request waits
       for it to finish */
    if (self->configure_pending) {
        RECT_SET(self->queued_area, x, y, w, h);
        self->queued_user = user;
        self->queued_final = final;
        self->configure_queued = TRUE;
        return;
    }

    /* find the new x, y, width, and height (and logical size) */
    client_try_configure(self, &x, &y, &w, &h, &logicalw, &logicalh, user);

    /* set the logical size if things changed */
    if (!(w == self->area.width && h == self->area.height))
        SIZE_SET(self->logical_size, logicalw, logicalh);

    resized = (w != self->area.width || h != self->area.height);

    /* for app-requested resizes, always resize if 'resized' is true.
       for user-requested ones, only resize if final is true, or when
       resizing in redraw mode */
    send_resize_client = ((!user && resized) ||
                          (user && (final ||
                                    (resized && config_resize_redraw))));

    /* In X the window resizes right away and paints itself afterwards.  A
       Wayland client resizes when it draws, so ask it to and show the
       frame at the new size when it has done so. */
    if (resized && send_resize_client && self->frame->visible &&
        self->impl->configure(self, x, y, w, h))
    {
        RECT_SET(self->pending_area, x, y, w, h);
        self->pending_old_area = self->area;
        self->pending_user = user;
        self->configure_pending = TRUE;
        self->configure_timeout =
            g_timeout_add(CONFIGURE_TIMEOUT, configure_timeout, self);
        return;
    }

    if (send_resize_client || !resized)
        /* tell the client where it is (Xwayland uses root coordinates) */
        self->impl->configure(self, x, y, w, h);

    client_apply_configure(self, x, y, w, h, user, final);
}

void client_committed(ObClient *self, gint width, gint height)
{
    self->committed_size.width = width;
    self->committed_size.height = height;

    if (!self->managed || !self->frame)
        return;

    if (self->configure_pending) {
        if (self->impl->configure_done(self, width, height) ||
            !self->configure_timeout)
            client_finish_configure(self, width, height);
    }
    else if (width > 0 && height > 0 &&
             (width != self->area.width || height != self->area.height) &&
             !self->shaded && (!moveresize_in_progress ||
                               moveresize_client != self))
    {
        /* the client changed size on its own, which a Wayland client can
           do (a configure only suggests a size for a floating window) */
        client_apply_configure(self, self->area.x, self->area.y,
                               width, height, FALSE, TRUE);
    }
}

/* This is the part of Openbox's client_configure() that moves and resizes
   the frame */
static void client_apply_configure(ObClient *self, gint x, gint y,
                                   gint w, gint h, gboolean user,
                                   gboolean final)
{
    Rect oldframe, oldclient;
    gboolean moved = FALSE, resized = FALSE;
    gboolean fmoved, fresized;
    guint fdecor = self->frame->decorations;
    gboolean fhorz = self->frame->max_horz;
    gboolean fvert = self->frame->max_vert;
    gboolean fshaded = self->frame->shaded;

    /* figure out if we moved or resized or what */
    moved = (x != self->area.x || y != self->area.y);
    resized = (w != self->area.width || h != self->area.height);

    oldframe = self->frame->area;
    oldclient = self->area;
    RECT_SET(self->area, x, y, w, h);

    /* find the frame's dimensions and move/resize it */
    fmoved = moved;
    fresized = resized;

    /* if decorations changed, then readjust everything for the frame */
    if (self->decorations != fdecor ||
        self->max_horz != fhorz || self->max_vert != fvert ||
        self->shaded != fshaded)
    {
        fmoved = fresized = TRUE;
    }

    /* adjust the frame */
    if (fmoved || fresized) {
        gulong ignore_start;
        if (!user)
            ignore_start = event_start_ignore_all_enters();

        frame_adjust_area(self->frame, fmoved, fresized, FALSE);

        if (!user)
            event_end_ignore_all_enters(ignore_start);
    }

    if (resized)
        frame_adjust_client_area(self->frame);

    if (!user || final) {
        /* we have reset the client to 0 border width, so don't include
           it in these coords */
        POINT_SET(self->root_pos,
                  self->frame->area.x + self->frame->size.left -
                  self->border_width,
                  self->frame->area.y + self->frame->size.top -
                  self->border_width);
    }

    if (moved || resized)
        toplevel_update_geometry(self);

    /* if it moved between monitors, then this can affect the stacking
       layer of this window or others - for fullscreen windows.
       also if it changed to/from oldschool fullscreen then its layer may
       change

       watch out tho, don't try change stacking stuff if the window is no
       longer being managed !
    */
    if (self->managed &&
        (screen_find_monitor(&self->frame->area) !=
         screen_find_monitor(&oldframe) ||
         (final && (client_is_oldfullscreen(self, &oldclient) !=
                    client_is_oldfullscreen(self, &self->area)))))
    {
        client_calc_layer(self);
    }
}

void client_fullscreen(ObClient *self, gboolean fs)
{
    gint x, y, w, h;

    if (!(self->functions & OB_CLIENT_FUNC_FULLSCREEN) || /* can't */
        self->fullscreen == fs) return;                   /* already done */

    self->fullscreen = fs;
    client_change_state(self); /* change the state hints on the client */

    if (fs) {
        self->pre_fullscreen_area = self->area;
        self->pre_fullscreen_max_horz = self->max_horz;
        self->pre_fullscreen_max_vert = self->max_vert;

        /* if the window is maximized, its area isn't all that meaningful.
           save its premax area instead. */
        if (self->max_horz) {
            self->pre_fullscreen_area.x = self->pre_max_area.x;
            self->pre_fullscreen_area.width = self->pre_max_area.width;
        }
        if (self->max_vert) {
            self->pre_fullscreen_area.y = self->pre_max_area.y;
            self->pre_fullscreen_area.height = self->pre_max_area.height;
        }

        /* these will help configure_full figure out where to fullscreen
           the window */
        x = self->area.x;
        y = self->area.y;
        w = self->area.width;
        h = self->area.height;
    } else {
        g_assert(self->pre_fullscreen_area.width > 0 &&
                 self->pre_fullscreen_area.height > 0);

        self->max_horz = self->pre_fullscreen_max_horz;
        self->max_vert = self->pre_fullscreen_max_vert;
        if (self->max_horz) {
            self->pre_max_area.x = self->pre_fullscreen_area.x;
            self->pre_max_area.width = self->pre_fullscreen_area.width;
        }
        if (self->max_vert) {
            self->pre_max_area.y = self->pre_fullscreen_area.y;
            self->pre_max_area.height = self->pre_fullscreen_area.height;
        }

        x = self->pre_fullscreen_area.x;
        y = self->pre_fullscreen_area.y;
        w = self->pre_fullscreen_area.width;
        h = self->pre_fullscreen_area.height;
        RECT_SET(self->pre_fullscreen_area, 0, 0, 0, 0);
    }

    ob_debug("Window %s going fullscreen (%d)",
             self->title, self->fullscreen);

    if (fs) {
        /* make sure the window is on some monitor */
        client_find_onscreen(self, &x, &y, w, h, FALSE);
    }

    client_setup_decor_and_functions(self, FALSE);
    client_move_resize(self, x, y, w, h);

    /* and adjust our layer/stacking. do this after resizing the window,
       and applying decorations, because windows which fill the screen are
       considered "fullscreen" and it affects their layer */
    client_calc_layer(self);

    if (fs) {
        /* try focus us when we go into fullscreen mode */
        client_focus(self);
    }
}

static void client_iconify_recursive(ObClient *self,
                                     gboolean iconic, gboolean curdesk,
                                     gboolean hide_animation)
{
    GSList *it;
    gboolean changed = FALSE;

    if (self->iconic != iconic) {
        ob_debug("%sconifying window: %s", (iconic ? "I" : "Uni"),
                 self->title);

        if (iconic) {
            /* don't let non-normal windows iconify along with their parents
               or whatever */
            if (client_normal(self)) {
                self->iconic = iconic;

                /* update the focus lists.. iconic windows go to the bottom of
                   the list. this will also call focus_cycle_addremove(). */
                focus_order_to_bottom(self);

                changed = TRUE;
            }
        } else {
            self->iconic = iconic;

            if (curdesk && self->desktop != screen_desktop &&
                self->desktop != DESKTOP_ALL)
                client_set_desktop(self, screen_desktop, FALSE, FALSE);

            /* this puts it after the current focused window, this will
               also cause focus_cycle_addremove() to be called for the
               client */
            focus_order_like_new(self);

            changed = TRUE;
        }
    }

    if (changed) {
        client_change_state(self);
        if (config_animate_iconify && !hide_animation)
            frame_begin_iconify_animation(self->frame, iconic);
        /* do this after starting the animation so it doesn't flash */
        client_showhide(self);
    }

    /* iconify all direct transients, and deiconify all transients
       (non-direct too) */
    for (it = self->transients; it; it = g_slist_next(it))
        if (it->data != self)
            if (client_is_direct_child(self, it->data) || !iconic)
                client_iconify_recursive(it->data, iconic, curdesk,
                                         hide_animation);
}

void client_iconify(ObClient *self, gboolean iconic, gboolean curdesk,
                    gboolean hide_animation)
{
    if (self->functions & OB_CLIENT_FUNC_ICONIFY || !iconic) {
        /* move up the transient chain as far as possible first */
        self = client_search_top_direct_parent(self);
        client_iconify_recursive(self, iconic, curdesk, hide_animation);
    }
}

void client_maximize(ObClient *self, gboolean max, gint dir)
{
    gint x, y, w, h;

    g_assert(dir == 0 || dir == 1 || dir == 2);
    if (!(self->functions & OB_CLIENT_FUNC_MAXIMIZE) && max) return;/* can't */

    /* check if already done */
    if (max) {
        if (dir == 0 && self->max_horz && self->max_vert) return;
        if (dir == 1 && self->max_horz) return;
        if (dir == 2 && self->max_vert) return;
    } else {
        if (dir == 0 && !self->max_horz && !self->max_vert) return;
        if (dir == 1 && !self->max_horz) return;
        if (dir == 2 && !self->max_vert) return;
    }

    /* these will help configure_full figure out which screen to fill with
       the window */
    x = self->area.x;
    y = self->area.y;
    w = self->area.width;
    h = self->area.height;

    if (max) {
        if ((dir == 0 || dir == 1) && !self->max_horz) { /* horz */
            RECT_SET(self->pre_max_area,
                     self->area.x, self->pre_max_area.y,
                     self->area.width, self->pre_max_area.height);
        }
        if ((dir == 0 || dir == 2) && !self->max_vert) { /* vert */
            RECT_SET(self->pre_max_area,
                     self->pre_max_area.x, self->area.y,
                     self->pre_max_area.width, self->area.height);
        }
    } else {
        if ((dir == 0 || dir == 1) && self->max_horz) { /* horz */
            g_assert(self->pre_max_area.width > 0);

            x = self->pre_max_area.x;
            w = self->pre_max_area.width;

            RECT_SET(self->pre_max_area, 0, self->pre_max_area.y,
                     0, self->pre_max_area.height);
        }
        if ((dir == 0 || dir == 2) && self->max_vert) { /* vert */
            g_assert(self->pre_max_area.height > 0);

            y = self->pre_max_area.y;
            h = self->pre_max_area.height;

            RECT_SET(self->pre_max_area, self->pre_max_area.x, 0,
                     self->pre_max_area.width, 0);
        }
    }

    if (dir == 0 || dir == 1) /* horz */
        self->max_horz = max;
    if (dir == 0 || dir == 2) /* vert */
        self->max_vert = max;

    if (max) {
        /* make sure the window is on some monitor */
        client_find_onscreen(self, &x, &y, w, h, FALSE);
        /* owbox: a maximized window is not snapped anymore */
        self->snap_edges = 0;
    }

    client_change_state(self); /* change the state hints on the client */

    client_setup_decor_and_functions(self, FALSE);
    client_move_resize(self, x, y, w, h);
}

void client_shade(ObClient *self, gboolean shade)
{
    if ((!(self->functions & OB_CLIENT_FUNC_SHADE) &&
         shade) ||                         /* can't shade */
        self->shaded == shade) return;     /* already done */

    self->shaded = shade;
    client_change_state(self);
    client_change_wm_state(self); /* the window is being hidden/shown */
    /* resize the frame to just the titlebar */
    frame_adjust_area(self->frame, FALSE, TRUE, FALSE);
}

void client_ping_event(ObClient *self, gboolean dead)
{
    if (self->not_responding != dead) {
        self->not_responding = dead;
        client_update_title(self);

        if (dead)
            /* the client isn't responding, so ask to kill it */
            client_prompt_kill(self);
        else {
            /* it came back to life ! */

            if (self->kill_prompt) {
                prompt_unref(self->kill_prompt);
                self->kill_prompt = NULL;
            }

            self->kill_level = 0;
        }
    }
}

void client_close(ObClient *self)
{
    if (!(self->functions & OB_CLIENT_FUNC_CLOSE)) return;

    /* if closing an internal obprompt, that is just cancelling it */
    if (self->prompt) {
        prompt_cancel(self->prompt);
        return;
    }

    /* request the client to close */
    self->impl->close(self);

    /* if we already know the window isn't responding (maybe they clicked
       no in the kill dialog but it hasn't come back to life), then show
       the kill dialog */
    if (self->not_responding)
        client_prompt_kill(self);
}

#define OB_KILL_RESULT_NO 0
#define OB_KILL_RESULT_YES 1

static gboolean client_kill_requested(ObPrompt *p, gint result, gpointer data)
{
    ObClient *self = data;

    if (result == OB_KILL_RESULT_YES)
        client_kill(self);
    return TRUE; /* call the cleanup func */
}

static void client_kill_cleanup(ObPrompt *p, gpointer data)
{
    ObClient *self = data;

    g_assert(p == self->kill_prompt);

    prompt_unref(self->kill_prompt);
    self->kill_prompt = NULL;
}

static void client_prompt_kill(ObClient *self)
{
    /* check if we're already prompting */
    if (!self->kill_prompt) {
        ObPromptAnswer answers[] = {
            { 0, OB_KILL_RESULT_NO },
            { 0, OB_KILL_RESULT_YES }
        };
        gchar *m;
        const gchar *y, *title;

        title = self->original_title;
        if (title[0] == '\0') {
            /* empty string, so use its parent */
            ObClient *p = client_search_top_direct_parent(self);
            if (p) title = p->original_title;
        }

        if (client_on_localhost(self)) {
            const gchar *sig;

            if (self->kill_level == 0)
                sig = "terminate";
            else
                sig = "kill";

            m = g_strdup_printf
                (_("The window \"%s\" does not seem to be responding.  Do you want to force it to exit by sending the %s signal?"),
                 title, sig);
            y = _("End Process");
        }
        else {
            m = g_strdup_printf
                (_("The window \"%s\" does not seem to be responding.  Do you want to disconnect it from the display?"),
                 title);
            y = _("Disconnect");
        }
        /* set the dialog buttons' text */
        answers[0].text = _("Cancel");  /* "no" */
        answers[1].text = y;            /* "yes" */

        self->kill_prompt = prompt_new(m, NULL, answers,
                                       sizeof(answers)/sizeof(answers[0]),
                                       OB_KILL_RESULT_NO, /* default = no */
                                       OB_KILL_RESULT_NO, /* cancel = no */
                                       client_kill_requested,
                                       client_kill_cleanup,
                                       self);
        g_free(m);
    }

    prompt_show(self->kill_prompt, self, TRUE);
}

void client_kill(ObClient *self)
{
    pid_t pid;

    /* don't kill our own windows */
    if (self->prompt) return;

    pid = self->impl->get_pid ? self->impl->get_pid(self) : 0;
    if (pid > 0 && pid != getpid()) {
        if (self->kill_level == 0) {
            ob_debug("killing window %s with pid %lu, with SIGTERM",
                     self->title, (gulong)pid);
            kill(pid, SIGTERM);
            ++self->kill_level;

            /* show that we're trying to kill it */
            client_update_title(self);
        }
        else {
            ob_debug("killing window %s with pid %lu, with SIGKILL",
                     self->title, (gulong)pid);
            kill(pid, SIGKILL); /* kill -9 */
        }
    }
    else {
        /* disconnect the client */
        struct wlr_surface *s = self->impl->surface(self);
        if (s)
            wl_client_destroy(wl_resource_get_client(s->resource));
    }
}

void client_hilite(ObClient *self, gboolean hilite)
{
    if (self->demands_attention == hilite)
        return; /* no change */

    /* don't allow focused windows to hilite */
    self->demands_attention = hilite && !client_focused(self);
    if (self->frame != NULL) { /* if we're mapping, just set the state */
        if (self->demands_attention) {
            frame_flash_start(self->frame);

            /* if the window is on another desktop then raise it and make it
               the most recently used window */
            if (self->desktop != screen_desktop &&
                self->desktop != DESKTOP_ALL)
            {
                stacking_raise(CLIENT_AS_WINDOW(self));
                focus_order_to_top(self);
            }
        }
        else
            frame_flash_stop(self->frame);
        client_change_state(self);
    }
}

static void client_set_desktop_recursive(ObClient *self,
                                         guint target,
                                         gboolean donthide,
                                         gboolean dontraise)
{
    guint old;
    GSList *it;

    if (target != self->desktop && self->type != OB_CLIENT_TYPE_DESKTOP) {

        ob_debug("Setting desktop %u", target+1);

        g_assert(target < screen_num_desktops || target == DESKTOP_ALL);

        old = self->desktop;
        self->desktop = target;
        toplevel_update_desktop(self);
        /* the frame can display the current desktop state */
        frame_adjust_state(self->frame);
        /* 'move' the window to the new desktop */
        if (!donthide)
            client_hide(self);
        client_show(self);
        /* raise if it was not already on the desktop */
        if (old != DESKTOP_ALL && !dontraise)
            stacking_raise(CLIENT_AS_WINDOW(self));
        if (STRUT_EXISTS(self->strut))
            screen_update_areas();
        else
            /* the new desktop's geometry may be different, so we may need to
               resize, for example if we are maximized */
            client_reconfigure(self, FALSE);

        focus_cycle_addremove(self, FALSE);
    }

    /* move all transients */
    for (it = self->transients; it; it = g_slist_next(it))
        if (it->data != self)
            if (client_is_direct_child(self, it->data))
                client_set_desktop_recursive(it->data, target,
                                             donthide, dontraise);
}

void client_set_desktop(ObClient *self, guint target,
                        gboolean donthide, gboolean dontraise)
{
    self = client_search_top_direct_parent(self);
    client_set_desktop_recursive(self, target, donthide, dontraise);

    focus_cycle_addremove(NULL, TRUE);
}

gboolean client_is_direct_child(ObClient *parent, ObClient *child)
{
    while (child != parent && (child = client_direct_parent(child)));
    return child == parent;
}

ObClient *client_search_modal_child(ObClient *self)
{
    GSList *it;
    ObClient *ret;

    for (it = self->transients; it; it = g_slist_next(it)) {
        ObClient *c = it->data;
        if ((ret = client_search_modal_child(c))) return ret;
        if (c->modal) return c;
    }
    return NULL;
}

gboolean client_validate(ObClient *self)
{
    return self->managed;
}

ObClient *client_focus_target(ObClient *self)
{
    ObClient *child = NULL;

    child = client_search_modal_child(self);
    if (child) return child;
    return self;
}

gboolean client_can_focus(ObClient *self)
{
    /* choose the correct target */
    self = client_focus_target(self);

    if (!self->frame->visible)
        return FALSE;

    if (!self->can_focus)
        return FALSE;

    return TRUE;
}

gboolean client_focus(ObClient *self)
{
    if (!client_validate(self)) return FALSE;

    /* we might not focus this window, so if we have modal children which would
       be focused instead, bring them to this desktop */
    client_bring_modal_windows(self);

    /* choose the correct target */
    self = client_focus_target(self);

    if (!client_can_focus(self)) {
        ob_debug_type(OB_DEBUG_FOCUS,
                      "Client %s can't be focused", self->title);
        return FALSE;
    }

    /* if we have helper windows they should be there with the window */
    client_bring_helper_windows(self);

    ob_debug_type(OB_DEBUG_FOCUS,
                  "Focusing client \"%s\" at time %u",
                  self->title, event_time());

    /* if using focus_delay, stop the timer now so that focus doesn't
       go moving on us */
    event_halt_focus_delay();

    /* this sends the client a keyboard enter, and calls the focus in/out
       handlers in event.c, like the FocusIn event does for Openbox */
    return seat_focus_client(self);
}

static void client_present(ObClient *self, gboolean here, gboolean raise,
                           gboolean unshade)
{
    if (client_normal(self) && screen_showing_desktop())
        screen_show_desktop(FALSE, self);
    if (self->iconic)
        client_iconify(self, FALSE, here, FALSE);
    if (self->desktop != DESKTOP_ALL &&
        self->desktop != screen_desktop)
    {
        if (here)
            client_set_desktop(self, screen_desktop, FALSE, TRUE);
        else
            screen_set_desktop(self->desktop, FALSE);
    } else if (!self->frame->visible)
        /* if its not visible for other reasons, then don't mess
           with it */
        return;
    if (self->shaded && unshade)
        client_shade(self, FALSE);
    if (raise)
        stacking_raise(CLIENT_AS_WINDOW(self));

    client_focus(self);
}

/* this function exists to map to the net_active_window message in the ewmh */
void client_activate(ObClient *self, gboolean desktop,
                     gboolean here, gboolean raise,
                     gboolean unshade, gboolean user)
{
    self = client_focus_target(self);

    if (client_can_steal_focus(self, desktop, user, event_time(), 0))
        client_present(self, here, raise, unshade);
    else
        client_hilite(self, TRUE);
}

static void client_bring_windows_recursive(ObClient *self,
                                           guint desktop,
                                           gboolean helpers,
                                           gboolean modals,
                                           gboolean iconic)
{
    GSList *it;

    for (it = self->transients; it; it = g_slist_next(it))
        client_bring_windows_recursive(it->data, desktop,
                                       helpers, modals, iconic);

    if (((helpers && client_helper(self)) ||
         (modals && self->modal)) &&
        (!screen_compare_desktops(self->desktop, desktop) ||
         (iconic && self->iconic)))
    {
        if (iconic && self->iconic)
            client_iconify(self, FALSE, TRUE, FALSE);
        else
            client_set_desktop(self, desktop, FALSE, FALSE);
    }
}

void client_bring_helper_windows(ObClient *self)
{
    client_bring_windows_recursive(self, self->desktop, TRUE, FALSE, FALSE);
}

void client_bring_modal_windows(ObClient *self)
{
    client_bring_windows_recursive(self, self->desktop, FALSE, TRUE, TRUE);
}

gboolean client_focused(ObClient *self)
{
    return self == focus_client;
}

RrImage* client_icon(ObClient *self)
{
    RrImage *ret = NULL;

    if (self->icon_set)
        ret = self->icon_set;
    else if (self->parents) {
        GSList *it;
        for (it = self->parents; it && !ret; it = g_slist_next(it))
            ret = client_icon(it->data);
    }
    if (!ret)
        ret = client_default_icon;
    return ret;
}

void client_set_layer(ObClient *self, gint layer)
{
    if (layer < 0) {
        self->below = TRUE;
        self->above = FALSE;
    } else if (layer == 0) {
        self->below = self->above = FALSE;
    } else {
        self->below = FALSE;
        self->above = TRUE;
    }
    client_calc_layer(self);
    client_change_state(self); /* reflect this in the state hints */
}

void client_set_undecorated(ObClient *self, gboolean undecorated)
{
    if (self->undecorated != undecorated &&
        /* don't let it undecorate if the function is missing, but let
           it redecorate */
        (self->functions & OB_CLIENT_FUNC_UNDECORATE || !undecorated))
    {
        self->undecorated = undecorated;
        client_setup_decor_and_functions(self, TRUE);
        client_change_state(self); /* reflect this in the state hints */
    }
}

guint client_monitor(ObClient *self)
{
    return screen_find_monitor(&self->frame->area);
}

ObClient *client_direct_parent(ObClient *self)
{
    if (!self->parents) return NULL;
    if (self->transient_for_group) return NULL;
    return self->parents->data;
}

ObClient *client_search_top_direct_parent(ObClient *self)
{
    ObClient *p;
    while ((p = client_direct_parent(self))) self = p;
    return self;
}

static GSList *client_search_all_top_parents_internal(ObClient *self,
                                                      gboolean bylayer,
                                                      ObStackingLayer layer)
{
    GSList *ret;
    ObClient *p;

    /* move up the direct transient chain as far as possible */
    while ((p = client_direct_parent(self)) &&
           (!bylayer || p->layer == layer))
        self = p;

    if (!self->parents)
        ret = g_slist_prepend(NULL, self);
    else
        ret = g_slist_copy(self->parents);

    return ret;
}

GSList *client_search_all_top_parents(ObClient *self)
{
    return client_search_all_top_parents_internal(self, FALSE, 0);
}

GSList *client_search_all_top_parents_layer(ObClient *self)
{
    return client_search_all_top_parents_internal(self, TRUE, self->layer);
}

ObClient *client_search_focus_parent(ObClient *self)
{
    GSList *it;

    for (it = self->parents; it; it = g_slist_next(it))
        if (client_focused(it->data)) return it->data;

    return NULL;
}

ObClient *client_search_focus_parent_full(ObClient *self)
{
    GSList *it;
    ObClient *ret = NULL;

    for (it = self->parents; it; it = g_slist_next(it)) {
        if (client_focused(it->data))
            ret = it->data;
        else
            ret = client_search_focus_parent_full(it->data);
        if (ret) break;
    }
    return ret;
}

ObClient *client_search_parent(ObClient *self, ObClient *search)
{
    GSList *it;

    for (it = self->parents; it; it = g_slist_next(it))
        if (it->data == search) return search;

    return NULL;
}

ObClient *client_search_transient(ObClient *self, ObClient *search)
{
    GSList *sit;

    for (sit = self->transients; sit; sit = g_slist_next(sit)) {
        if (sit->data == search)
            return search;
        if (client_search_transient(sit->data, search))
            return search;
    }
    return NULL;
}

static void detect_edge(Rect area, ObDirection dir,
                        gint my_head, gint my_size,
                        gint my_edge_start, gint my_edge_size,
                        gint *dest, gboolean *near_edge)
{
    gint edge_start, edge_size, head, tail;
    gboolean skip_head = FALSE, skip_tail = FALSE;

    switch (dir) {
        case OB_DIRECTION_NORTH:
        case OB_DIRECTION_SOUTH:
            edge_start = area.x;
            edge_size = area.width;
            break;
        case OB_DIRECTION_EAST:
        case OB_DIRECTION_WEST:
            edge_start = area.y;
            edge_size = area.height;
            break;
        default:
            g_assert_not_reached();
    }

    /* do we collide with this window? */
    if (!RANGES_INTERSECT(my_edge_start, my_edge_size,
                edge_start, edge_size))
        return;

    switch (dir) {
        case OB_DIRECTION_NORTH:
            head = RECT_BOTTOM(area);
            tail = RECT_TOP(area);
            break;
        case OB_DIRECTION_SOUTH:
            head = RECT_TOP(area);
            tail = RECT_BOTTOM(area);
            break;
        case OB_DIRECTION_WEST:
            head = RECT_RIGHT(area);
            tail = RECT_LEFT(area);
            break;
        case OB_DIRECTION_EAST:
            head = RECT_LEFT(area);
            tail = RECT_RIGHT(area);
            break;
        default:
            g_assert_not_reached();
    }
    switch (dir) {
        case OB_DIRECTION_NORTH:
        case OB_DIRECTION_WEST:
            /* check if our window is past the head of this window */
            if (my_head <= head + 1)
                skip_head = TRUE;
            /* check if our window's tail is past the tail of this window */
            if (my_head + my_size - 1 <= tail)
                skip_tail = TRUE;
            /* check if the head of this window is closer than the previously
               chosen edge (take into account that the previously chosen
               edge might have been a tail, not a head) */
            if (head + (*near_edge ? 0 : my_size) <= *dest)
                skip_head = TRUE;
            /* check if the tail of this window is closer than the previously
               chosen edge (take into account that the previously chosen
               edge might have been a head, not a tail) */
            if (tail - (!*near_edge ? 0 : my_size) <= *dest)
                skip_tail = TRUE;
            break;
        case OB_DIRECTION_SOUTH:
        case OB_DIRECTION_EAST:
            /* check if our window is past the head of this window */
            if (my_head >= head - 1)
                skip_head = TRUE;
            /* check if our window's tail is past the tail of this window */
            if (my_head - my_size + 1 >= tail)
                skip_tail = TRUE;
            /* check if the head of this window is closer than the previously
               chosen edge (take into account that the previously chosen
               edge might have been a tail, not a head) */
            if (head - (*near_edge ? 0 : my_size) >= *dest)
                skip_head = TRUE;
            /* check if the tail of this window is closer than the previously
               chosen edge (take into account that the previously chosen
               edge might have been a head, not a tail) */
            if (tail + (!*near_edge ? 0 : my_size) >= *dest)
                skip_tail = TRUE;
            break;
        default:
            g_assert_not_reached();
    }

    ob_debug("my head %d size %d", my_head, my_size);
    ob_debug("head %d tail %d dest %d", head, tail, *dest);
    if (!skip_head) {
        ob_debug("using near edge %d", head);
        *dest = head;
        *near_edge = TRUE;
    }
    else if (!skip_tail) {
        ob_debug("using far edge %d", tail);
        *dest = tail;
        *near_edge = FALSE;
    }
}

void client_find_edge_directional(ObClient *self, ObDirection dir,
                                  gint my_head, gint my_size,
                                  gint my_edge_start, gint my_edge_size,
                                  gint *dest, gboolean *near_edge)
{
    GList *it;
    Rect *a;
    gint edge;
    guint i;

    a = screen_area(self->desktop, SCREEN_AREA_ALL_MONITORS,
                    &self->frame->area);

    switch (dir) {
    case OB_DIRECTION_NORTH:
        edge = RECT_TOP(*a) - 1;
        break;
    case OB_DIRECTION_SOUTH:
        edge = RECT_BOTTOM(*a) + 1;
        break;
    case OB_DIRECTION_EAST:
        edge = RECT_RIGHT(*a) + 1;
        break;
    case OB_DIRECTION_WEST:
        edge = RECT_LEFT(*a) - 1;
        break;
    default:
        g_assert_not_reached();
    }
    /* default to the far edge, then narrow it down */
    *dest = edge;
    *near_edge = TRUE;

    /* search for edges of monitors */
    for (i = 0; i < screen_num_monitors; ++i) {
        Rect *area = screen_area(self->desktop, i, NULL);
        detect_edge(*area, dir, my_head, my_size, my_edge_start,
                    my_edge_size, dest, near_edge);
        g_slice_free(Rect, area);
    }

    /* search for edges of clients */
    for (it = client_list; it; it = g_list_next(it)) {
        ObClient *cur = it->data;

        /* skip windows to not bump into */
        if (cur == self)
            continue;
        if (cur->iconic)
            continue;
        if (self->desktop != cur->desktop && cur->desktop != DESKTOP_ALL &&
            cur->desktop != screen_desktop)
            continue;

        ob_debug("trying window %s", cur->title);

        detect_edge(cur->frame->area, dir, my_head, my_size, my_edge_start,
                    my_edge_size, dest, near_edge);
    }

    g_slice_free(Rect, a);
}

void client_find_move_directional(ObClient *self, ObDirection dir,
                                  gint *x, gint *y)
{
    gint head, size;
    gint e, e_start, e_size;
    gboolean near;

    switch (dir) {
    case OB_DIRECTION_EAST:
        head = RECT_RIGHT(self->frame->area);
        size = self->frame->area.width;
        e_start = RECT_TOP(self->frame->area);
        e_size = self->frame->area.height;
        break;
    case OB_DIRECTION_WEST:
        head = RECT_LEFT(self->frame->area);
        size = self->frame->area.width;
        e_start = RECT_TOP(self->frame->area);
        e_size = self->frame->area.height;
        break;
    case OB_DIRECTION_NORTH:
        head = RECT_TOP(self->frame->area);
        size = self->frame->area.height;
        e_start = RECT_LEFT(self->frame->area);
        e_size = self->frame->area.width;
        break;
    case OB_DIRECTION_SOUTH:
        head = RECT_BOTTOM(self->frame->area);
        size = self->frame->area.height;
        e_start = RECT_LEFT(self->frame->area);
        e_size = self->frame->area.width;
        break;
    default:
        g_assert_not_reached();
    }

    client_find_edge_directional(self, dir, head, size,
                                 e_start, e_size, &e, &near);
    *x = self->frame->area.x;
    *y = self->frame->area.y;
    switch (dir) {
    case OB_DIRECTION_EAST:
        if (near) e -= self->frame->area.width;
        else      e++;
        *x = e;
        break;
    case OB_DIRECTION_WEST:
        if (near) e++;
        else      e -= self->frame->area.width;
        *x = e;
        break;
    case OB_DIRECTION_NORTH:
        if (near) e++;
        else      e -= self->frame->area.height;
        *y = e;
        break;
    case OB_DIRECTION_SOUTH:
        if (near) e -= self->frame->area.height;
        else      e++;
        *y = e;
        break;
    default:
        g_assert_not_reached();
    }
    frame_frame_gravity(self->frame, x, y);
}

void client_find_resize_directional(ObClient *self,
                                    ObDirection side,
                                    ObClientDirectionalResizeType resize_type,
                                    gint *x, gint *y, gint *w, gint *h)
{
    gint head;
    gint e, e_start, e_size, delta;
    gboolean near;
    ObDirection dir;

    gboolean grow;
    switch (resize_type) {
    case CLIENT_RESIZE_GROW:
        grow = TRUE;
        break;
    case CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE:
        grow = TRUE;
        break;
    case CLIENT_RESIZE_SHRINK:
        grow = FALSE;
        break;
    }

    switch (side) {
    case OB_DIRECTION_EAST:
        head = RECT_RIGHT(self->frame->area);
        switch (resize_type) {
        case CLIENT_RESIZE_GROW:
            head += self->size_inc.width - 1;
            break;
        case CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE:
            head -= 1;
            break;
        case CLIENT_RESIZE_SHRINK:
            break;
        }

        e_start = RECT_TOP(self->frame->area);
        e_size = self->frame->area.height;
        dir = grow ? OB_DIRECTION_EAST : OB_DIRECTION_WEST;
        break;
    case OB_DIRECTION_WEST:
        head = RECT_LEFT(self->frame->area);
        switch (resize_type) {
        case CLIENT_RESIZE_GROW:
            head -= self->size_inc.width - 1;
            break;
        case CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE:
            head += 1;
            break;
        case CLIENT_RESIZE_SHRINK:
            break;
        }

        e_start = RECT_TOP(self->frame->area);
        e_size = self->frame->area.height;
        dir = grow ? OB_DIRECTION_WEST : OB_DIRECTION_EAST;
        break;
    case OB_DIRECTION_NORTH:
        head = RECT_TOP(self->frame->area);
        switch (resize_type) {
        case CLIENT_RESIZE_GROW:
            head -= self->size_inc.height - 1;
            break;
        case CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE:
            head += 1;
            break;
        case CLIENT_RESIZE_SHRINK:
            break;
        }

        e_start = RECT_LEFT(self->frame->area);
        e_size = self->frame->area.width;
        dir = grow ? OB_DIRECTION_NORTH : OB_DIRECTION_SOUTH;
        break;
    case OB_DIRECTION_SOUTH:
        head = RECT_BOTTOM(self->frame->area);
        switch (resize_type) {
        case CLIENT_RESIZE_GROW:
            head += self->size_inc.height - 1;
            break;
        case CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE:
            head -= 1;
            break;
        case CLIENT_RESIZE_SHRINK:
            break;
        }

        e_start = RECT_LEFT(self->frame->area);
        e_size = self->frame->area.width;
        dir = grow ? OB_DIRECTION_SOUTH : OB_DIRECTION_NORTH;
        break;
    default:
        g_assert_not_reached();
    }

    ob_debug("head %d dir %d", head, dir);
    client_find_edge_directional(self, dir, head, 1,
                                 e_start, e_size, &e, &near);
    ob_debug("edge %d", e);
    *x = self->frame->area.x;
    *y = self->frame->area.y;
    *w = self->frame->area.width;
    *h = self->frame->area.height;
    switch (side) {
    case OB_DIRECTION_EAST:
        if (grow == near) --e;
        delta = e - RECT_RIGHT(self->frame->area);
        *w += delta;
        break;
    case OB_DIRECTION_WEST:
        if (grow == near) ++e;
        delta = RECT_LEFT(self->frame->area) - e;
        *x -= delta;
        *w += delta;
        break;
    case OB_DIRECTION_NORTH:
        if (grow == near) ++e;
        delta = RECT_TOP(self->frame->area) - e;
        *y -= delta;
        *h += delta;
        break;
    case OB_DIRECTION_SOUTH:
        if (grow == near) --e;
        delta = e - RECT_BOTTOM(self->frame->area);
        *h += delta;
       break;
    default:
        g_assert_not_reached();
    }
    frame_frame_gravity(self->frame, x, y);
    *w -= self->frame->size.left + self->frame->size.right;
    *h -= self->frame->size.top + self->frame->size.bottom;
}

ObClient* client_under_pointer(void)
{
    gint x, y;
    GList *it;
    ObClient *ret = NULL;

    if (screen_pointer_pos(&x, &y)) {
        for (it = stacking_list; it; it = g_list_next(it)) {
            if (WINDOW_IS_CLIENT(it->data)) {
                ObClient *c = WINDOW_AS_CLIENT(it->data);
                if (c->frame->visible &&
                    /* check the desktop, this is done during desktop
                       switching and windows are shown/hidden status is not
                       reliable */
                    (c->desktop == screen_desktop ||
                     c->desktop == DESKTOP_ALL) &&
                    /* ignore all animating windows */
                    !frame_iconify_animating(c->frame) &&
                    RECT_CONTAINS(c->frame->area, x, y))
                {
                    ret = c;
                    break;
                }
            }
        }
    }
    return ret;
}

gboolean client_has_group_siblings(ObClient *self)
{
    GList *it;

    for (it = client_list; it; it = g_list_next(it))
        if (it->data != self && client_same_group(it->data, self))
            return TRUE;
    return FALSE;
}

gboolean client_has_relative(ObClient *self)
{
    return client_has_parent(self) ||
        client_has_group_siblings(self) ||
        client_has_children(self);
}

/*! Returns TRUE if the client is running on the same machine as Openbox */
/*! Returns TRUE if the client is running on the same machine as Openbox */
gboolean client_on_localhost(ObClient *self)
{
    return TRUE;
}

/* owbox: things the shell implementations and the frame call */

gboolean client_same_group(ObClient *a, ObClient *b)
{
    pid_t pa = a->impl->get_pid ? a->impl->get_pid(a) : 0;
    pid_t pb = b->impl->get_pid ? b->impl->get_pid(b) : 0;
    struct wlr_surface *sa, *sb;

    if (pa && pb)
        return pa == pb;
    sa = a->impl->surface(a);
    sb = b->impl->surface(b);
    return sa && sb &&
        wl_resource_get_client(sa->resource) ==
        wl_resource_get_client(sb->resource);
}

ObClient* client_from_surface(struct wlr_surface *surface)
{
    GList *it;
    struct wlr_surface *root;

    if (!surface) return NULL;
    root = wlr_surface_get_root_surface(surface);
    for (it = client_list; it; it = g_list_next(it)) {
        ObClient *c = it->data;
        struct wlr_surface *s = c->impl->surface(c);
        if (s && (s == surface || s == root))
            return c;
    }
    return NULL;
}

void client_set_geometry_offset(ObClient *self, gint x, gint y)
{
    if (x == self->geometry_x && y == self->geometry_y)
        return;
    self->geometry_x = x;
    self->geometry_y = y;
    if (self->frame)
        frame_adjust_client_position(self->frame);
}

void client_reparent_surfaces(ObClient *self, struct wlr_scene_tree *parent)
{
    if (parent) {
        wlr_scene_node_reparent(&self->surface_tree->node, parent);
        wlr_scene_node_raise_to_top(&self->surface_tree->node);
    } else {
        wlr_scene_node_reparent(&self->surface_tree->node,
                                server.unmanaged_tree);
        wlr_scene_node_set_enabled(&self->surface_tree->node, FALSE);
    }
}

void client_set_surface_position(ObClient *self, gint x, gint y)
{
    wlr_scene_node_set_position(&self->surface_tree->node,
                                x - self->geometry_x, y - self->geometry_y);
}

void client_set_surface_visible(ObClient *self, gboolean visible)
{
    wlr_scene_node_set_enabled(&self->surface_tree->node, visible);
    if (self->impl->set_visible)
        self->impl->set_visible(self, visible);
}

void client_frame_extents_changed(ObClient *self)
{
}

void client_request_maximize(ObClient *self, gboolean max)
{
    if (!self->frame) return;
    client_maximize(self, max, 0);
    /* xdg-shell requires a configure in reply even if nothing changed */
    client_change_state(self);
}

void client_request_fullscreen(ObClient *self, gboolean fs)
{
    if (!self->frame) return;
    client_fullscreen(self, fs);
    client_change_state(self);
}

void client_request_minimize(ObClient *self, gboolean min)
{
    if (!self->frame) return;
    client_iconify(self, min, TRUE, FALSE);
}

void client_request_moveresize(ObClient *self, guint32 corner)
{
    gint x, y;

    if (!self->frame || moveresize_in_progress) return;
    if (!seat_pointer_button_held()) return;
    screen_pointer_pos(&x, &y);
    moveresize_start(self, x, y, seat_pointer_held_button(), corner);
}

void client_focus_surface_changed(ObClient *self)
{
    if (client_focused(self))
        seat_focus_client(self);
}

