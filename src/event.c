/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   event.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2006        Mikael Magnusson
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

/* Openbox's event.c dispatches X events.  In owbox seat.c turns Wayland
   input into ObEvents and calls in here, and the X focus events become
   event_focus_changed().  The handling of the user input itself is kept
   from Openbox. */

#include "event.h"
#include "debug.h"
#include "window.h"
#include "openbox.h"
#include "client.h"
#include "config.h"
#include "screen.h"
#include "frame.h"
#include "grab.h"
#include "menu.h"
#include "prompt.h"
#include "menuframe.h"
#include "keyboard.h"
#include "mouse.h"
#include "focus.h"
#include "focus_cycle.h"
#include "moveresize.h"
#include "stacking.h"
#include "actions.h"
#include "seat.h"
#include "server.h"
#include "toplevel.h"
#include "obt/keyboard.h"

#include <glib.h>

typedef struct
{
    ObClient *client;
    guint32 time;
} ObFocusDelayData;

static gboolean event_handle_menu_input(ObEvent *e);
static void event_handle_menu(ObEvent *e);
static void event_handle_client(ObClient *c, ObEvent *e);
static gboolean event_handle_user_input(ObClient *client, ObEvent *e);

static void focus_delay_dest(gpointer data);
static void unfocus_delay_dest(gpointer data);
static gboolean focus_delay_func(gpointer data);
static gboolean unfocus_delay_func(gpointer data);
static void focus_delay_client_dest(ObClient *client, gpointer data);

guint32 event_last_user_time = 0;

/*! The time of the current event */
static guint32 event_curtime = 0;
/*! The source time that started the current event (user-provided, so not
  to be trusted) */
static guint32 event_sourcetime = 0;

/*! Nesting of event_start_ignore_all_enters() */
static gint ignore_enters = 0;
static guint focus_delay_timeout_id = 0;
static ObClient *focus_delay_timeout_client = NULL;
static guint unfocus_delay_timeout_id = 0;
static ObClient *unfocus_delay_timeout_client = NULL;
/*! The client whose titlebar buttons show a hover state */
static ObClient *hover_client = NULL;
/*! The client the pointer was last in, for leave events */
static ObClient *pointer_client = NULL;

extern guint button;

static void client_dest(ObClient *client, gpointer data)
{
    if (hover_client == client)
        hover_client = NULL;
    if (pointer_client == client)
        pointer_client = NULL;
}

void event_startup(gboolean reconfig)
{
    if (reconfig) return;

    client_add_destroy_notify(focus_delay_client_dest, NULL);
    client_add_destroy_notify(client_dest, NULL);
}

void event_shutdown(gboolean reconfig)
{
    if (reconfig) return;

    client_remove_destroy_notify(focus_delay_client_dest);
    client_remove_destroy_notify(client_dest);
}

/* The FocusIn and FocusOut handling of Openbox's event_process() */
void event_focus_changed(ObClient *old, ObClient *new_,
                         gboolean to_other_surface)
{
    if (old && old != new_) {
        ob_debug_type(OB_DEBUG_FOCUS, "Focus out of %s", old->title);
        if (old->frame)
            frame_adjust_focus(old->frame, FALSE);
        if (old->managed)
            old->impl->set_activated(old, FALSE);
        if (!new_)
            /* nothing is focused (it went to a panel or such) */
            focus_set_client(NULL);
    }
    if (new_) {
        ob_debug_type(OB_DEBUG_FOCUS, "Focus into %s", new_->title);
        new_->impl->set_activated(new_, TRUE);
        if (new_ != focus_client) {
            frame_adjust_focus(new_->frame, TRUE);
            focus_set_client(new_);
            client_calc_layer(new_);
            client_bring_helper_windows(new_);
        }
    } else if (!old && !to_other_surface)
        focus_set_client(NULL);

    toplevel_update_focus(old, new_);
}

void event_enter_client(ObClient *client)
{
    g_assert(config_focus_follow);

    if (ignore_enters) {
        ob_debug_type(OB_DEBUG_FOCUS, "Ignoring enter event on client %s",
                      client->title);
        return;
    }

    ob_debug_type(OB_DEBUG_FOCUS, "using enter event on client %s",
                  client->title);

    if (client_enter_focusable(client) && client_can_focus(client)) {
        if (config_focus_delay) {
            ObFocusDelayData *data;

            if (focus_delay_timeout_id)
                g_source_remove(focus_delay_timeout_id);

            data = g_slice_new(ObFocusDelayData);
            data->client = client;
            data->time = event_time();

            focus_delay_timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                        config_focus_delay,
                                                        focus_delay_func,
                                                        data,
                                                        focus_delay_dest);
            focus_delay_timeout_client = client;
        } else {
            ObFocusDelayData data;
            data.client = client;
            data.time = event_time();
            focus_delay_func(&data);
        }
    }
}

void event_leave_client(ObClient *client)
{
    g_assert(config_focus_follow);

    if (ignore_enters) {
        ob_debug_type(OB_DEBUG_FOCUS, "Ignoring leave event on client %s",
                      client->title);
        return;
    }

    if (client == focus_client) {
        if (config_focus_delay) {
            ObFocusDelayData *data;

            if (unfocus_delay_timeout_id)
                g_source_remove(unfocus_delay_timeout_id);

            data = g_slice_new(ObFocusDelayData);
            data->client = client;
            data->time = event_time();

            unfocus_delay_timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                          config_focus_delay,
                                                          unfocus_delay_func,
                                                          data,
                                                          unfocus_delay_dest);
            unfocus_delay_timeout_client = client;
        } else {
            ObFocusDelayData data;
            data.client = client;
            data.time = event_time();
            unfocus_delay_func(&data);
        }
    }
}

static gboolean *context_to_button(ObFrame *f, ObFrameContext con, gboolean press)
{
    if (press) {
        switch (con) {
        case OB_FRAME_CONTEXT_MAXIMIZE:
            return &f->max_press;
        case OB_FRAME_CONTEXT_CLOSE:
            return &f->close_press;
        case OB_FRAME_CONTEXT_ICONIFY:
            return &f->iconify_press;
        case OB_FRAME_CONTEXT_ALLDESKTOPS:
            return &f->desk_press;
        case OB_FRAME_CONTEXT_SHADE:
            return &f->shade_press;
        default:
            return NULL;
        }
    } else {
        switch (con) {
        case OB_FRAME_CONTEXT_MAXIMIZE:
            return &f->max_hover;
        case OB_FRAME_CONTEXT_CLOSE:
            return &f->close_hover;
        case OB_FRAME_CONTEXT_ICONIFY:
            return &f->iconify_hover;
        case OB_FRAME_CONTEXT_ALLDESKTOPS:
            return &f->desk_hover;
        case OB_FRAME_CONTEXT_SHADE:
            return &f->shade_hover;
        default:
            return NULL;
        }
    }
}

static void clear_hover(ObClient *client, gboolean press_too)
{
    ObFrame *f;

    if (!client || !client->frame) return;
    f = client->frame;
    if (f->max_hover || f->desk_hover || f->shade_hover ||
        f->iconify_hover || f->close_hover ||
        (press_too && (f->max_press || f->desk_press || f->shade_press ||
                       f->iconify_press || f->close_press)))
    {
        f->max_hover = f->desk_hover = f->shade_hover =
            f->iconify_hover = f->close_hover = FALSE;
        if (press_too)
            f->max_press = f->desk_press = f->shade_press =
                f->iconify_press = f->close_press = FALSE;
        frame_adjust_state(f);
    }
}

/* The frame's titlebar buttons are drawn pressed and hovered, this is the
   part of Openbox's event_handle_client() for button and motion events */
static void event_handle_client(ObClient *client, ObEvent *e)
{
    ObFrameContext con;
    gboolean *but;
    static gint px = -1, py = -1;
    static guint pb = 0;
    static ObFWin *pwin = NULL;
    static ObFrameContext pcon = OB_FRAME_CONTEXT_NONE;

    switch (e->type) {
    case OB_EVENT_BUTTON_PRESS:
        /* save where the press occured for the first button pressed */
        if (!pb) {
            pb = e->button;
            px = e->x;
            py = e->y;
            pwin = e->win;

            pcon = frame_context(client, e->win, px, py);
            pcon = mouse_button_frame_context(pcon, e->button,
                                              e->state);
        }
        /* fall through */
    case OB_EVENT_BUTTON_RELEASE:
        /* Wheel buttons don't draw because they are an instant click, so it
           is a waste of resources to go drawing it.
           if the user is doing an interactive thing, or has a menu open then
           the mouse is grabbed (possibly) and if we get these events we don't
           want to deal with them
        */
        if (!(e->button == 4 || e->button == 5) &&
            !grab_on_keyboard())
        {
            /* use where the press occured */
            con = frame_context(client, pwin, px, py);
            con = mouse_button_frame_context(con, e->button,
                                             e->state);

            /* button presses on CLIENT_CONTEXTs are not accompanied by a
               release because they are Replayed to the client */
            if ((e->type == OB_EVENT_BUTTON_RELEASE ||
                 CLIENT_CONTEXT(con, client)) &&
                e->button == pb)
            {
                pb = 0, px = py = -1, pcon = OB_FRAME_CONTEXT_NONE;
                pwin = NULL;
            }

            but = context_to_button(client->frame, con, TRUE);
            if (but) {
                /* a button is drawn pressed only while the pointer is in
                   it, like X does with its implicit grab */
                *but = (e->type == OB_EVENT_BUTTON_PRESS);
                frame_adjust_state(client->frame);
            }
        }
        break;
    case OB_EVENT_MOTION:
        /* when there is a grab on the pointer, we won't get enter/leave
           notifies, but we still get motion events */
        if (grab_on_pointer()) break;

        con = frame_context(client, e->win, e->x, e->y);
        switch (con) {
        case OB_FRAME_CONTEXT_MAXIMIZE:
        case OB_FRAME_CONTEXT_CLOSE:
        case OB_FRAME_CONTEXT_ICONIFY:
        case OB_FRAME_CONTEXT_ALLDESKTOPS:
        case OB_FRAME_CONTEXT_SHADE:
            /* entering a button, other buttons don't hover */
            but = context_to_button(client->frame, con, FALSE);
            if (but && !*but && !pb) {
                clear_hover(client, FALSE);
                *but = TRUE;
                frame_adjust_state(client->frame);
            }
            /* while pressed, the button is drawn pressed only when the
               pointer is over it */
            if (pb && pcon == con) {
                but = context_to_button(client->frame, con, TRUE);
                if (but && !*but) {
                    *but = TRUE;
                    frame_adjust_state(client->frame);
                }
            }
            break;
        default:
            /* we've left the button area inside the titlebar */
            clear_hover(client, FALSE);
            if (pb) {
                but = context_to_button(client->frame, pcon, TRUE);
                if (but && *but) {
                    *but = FALSE;
                    frame_adjust_state(client->frame);
                }
            }
            break;
        }
        break;
    default:
        break;
    }
}

void event_pointer_enter(ObClient *client, gboolean motion)
{
    ObClient *old = pointer_client;

    pointer_client = client;

    /* the LeaveNotify of the old frame */
    if (hover_client && hover_client != client) {
        clear_hover(hover_client, FALSE);
        hover_client = NULL;
    }

    if (grab_on_keyboard())
        return;
    /* enter events from windows moving around under the pointer are like
       NotifyUngrab ones, only used with underMouse */
    if (!motion && !(config_focus_follow && config_focus_under_mouse))
        return;

    if (old && config_focus_follow && !client) {
        if (config_focus_delay && focus_delay_timeout_id)
            g_source_remove(focus_delay_timeout_id);
        if (config_unfocus_leave)
            event_leave_client(old);
    }
    if (client && config_focus_follow) {
        if (config_focus_delay && unfocus_delay_timeout_id)
            g_source_remove(unfocus_delay_timeout_id);
        event_enter_client(client);
    }
}

gboolean event_key(ObEvent *e)
{
    event_curtime = e->time;
    event_sourcetime = 0;

    /* owbox's own dialogs get the keys when they are focused */
    if (focus_client && focus_client->prompt && !grab_on_keyboard() &&
        e->type == OB_EVENT_KEY_PRESS)
    {
        if (keyboard_event(focus_client, e))
            return TRUE;
        return prompt_key_event(focus_client->prompt, e);
    }
    return event_handle_user_input(NULL, e);
}

static ObPrompt* event_prompt(ObEvent *e)
{
    if (e->node && e->node->owner && WINDOW_IS_PROMPT(e->node->owner))
        return WINDOW_AS_PROMPT(e->node->owner);
    return NULL;
}

gboolean event_pointer(ObEvent *e)
{
    gboolean used;

    event_curtime = e->time;
    event_sourcetime = 0;

    /* Openbox's event_hack_mods(), the bindings don't include the buttons
       held (a release has its own button in the state) or the lock keys */
    e->state = obt_keyboard_only_modmasks(e->state);

    if (menu_frame_visible && e->type == OB_EVENT_MOTION)
        event_handle_menu(e);

    /* clicks on the buttons of owbox's dialogs */
    if (event_prompt(e) && !grab_on_pointer() && !menu_frame_visible &&
        prompt_mouse_event(event_prompt(e), e))
        return TRUE;

    if (e->client && e->client->frame) {
        if (e->type == OB_EVENT_MOTION && !hover_client)
            hover_client = e->client;
        event_handle_client(e->client, e);
    } else if (hover_client && e->type == OB_EVENT_MOTION) {
        clear_hover(hover_client, FALSE);
        hover_client = NULL;
    }

    if (e->type == OB_EVENT_MOTION && !grab_on_pointer() &&
        !moveresize_in_progress && !button && !actions_interactive_act_running())
    {
        /* plain motion without a grab is only used for the hovering above */
        return FALSE;
    }

    used = event_handle_user_input(e->client, e);
    return used;
}

static ObMenuFrame* find_active_menu(void)
{
    GList *it;
    ObMenuFrame *ret = NULL;

    for (it = menu_frame_visible; it; it = g_list_next(it)) {
        ret = it->data;
        if (ret->selected)
            break;
        ret = NULL;
    }
    return ret;
}

static ObMenuFrame* find_active_or_last_menu(void)
{
    ObMenuFrame *ret = NULL;

    ret = find_active_menu();
    if (!ret && menu_frame_visible)
        ret = menu_frame_visible->data;
    return ret;
}

static gboolean event_handle_menu_input(ObEvent *ev)
{
    gboolean ret = FALSE;

    if (ev->type == OB_EVENT_BUTTON_RELEASE ||
        ev->type == OB_EVENT_BUTTON_PRESS)
    {
        ObMenuEntryFrame *e;

        if ((ev->button < 4 || ev->button > 5) &&
            ((ev->type == OB_EVENT_BUTTON_RELEASE &&
              menu_hide_delay_reached()) ||
             ev->type == OB_EVENT_BUTTON_PRESS))
        {
            if ((e = menu_entry_frame_under(ev->x_root, ev->y_root)))
            {
                if (ev->type == OB_EVENT_BUTTON_PRESS) {
                    /* We know this is a new press, so we don't have to
                     * block release events anymore */
                    menu_hide_delay_reset();

                    if (e->frame->child)
                        menu_frame_select(e->frame->child, NULL, TRUE);
                }
                menu_frame_select(e->frame, e, TRUE);
                if (ev->type == OB_EVENT_BUTTON_RELEASE)
                    menu_entry_frame_execute(e, ev->state);
            }
            else
                menu_frame_hide_all();
        }
        ret = TRUE;
    }
    else if (ev->type == OB_EVENT_KEY_PRESS ||
             ev->type == OB_EVENT_KEY_RELEASE)
    {
        guint mods;
        ObMenuFrame *frame;
        const guint ctrl =
            obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_CONTROL);

        /* get the modifiers */
        mods = obt_keyboard_only_modmasks(ev->state);

        frame = find_active_or_last_menu();
        if (frame == NULL)
            g_assert_not_reached(); /* there is no active menu */

        /* Allow control while going thru the menu */
        else if (ev->type == OB_EVENT_KEY_PRESS && (mods & ~ctrl) == 0) {
            gunichar unikey;
            xkb_keysym_t sym;

            frame->got_press = TRUE;
            frame->press_keycode = ev->keycode;
            frame->press_doexec = FALSE;

            sym = obt_keyboard_keypress_to_keysym(ev);

            if (sym == XK_Escape) {
                menu_frame_hide_all();
                ret = TRUE;
            }

            else if (sym == XK_Left) {
                /* Left goes to the parent menu */
                if (frame->parent) {
                    /* remove focus from the child */
                    menu_frame_select(frame, NULL, TRUE);
                    /* and put it in the parent */
                    menu_frame_select(frame->parent, frame->parent->selected,
                                      TRUE);
                }
                ret = TRUE;
            }

            else if (sym == XK_Right || sym == XK_Return || sym == XK_KP_Enter)
            {
                /* Right and enter goes to the selected submenu.
                   Enter executes instead if it's not on a submenu. */

                if (frame->selected) {
                    const ObMenuEntryType t = frame->selected->entry->type;

                    if (t == OB_MENU_ENTRY_TYPE_SUBMENU) {
                        /* make sure it is visible */
                        menu_frame_select(frame, frame->selected, TRUE);
                        /* move focus to the child menu */
                        menu_frame_select_next(frame->child);
                    }
                    else if (sym != XK_Right) {
                        frame->press_doexec = TRUE;
                    }
                }
                ret = TRUE;
            }

            else if (sym == XK_Up) {
                menu_frame_select_previous(frame);
                ret = TRUE;
            }

            else if (sym == XK_Down) {
                menu_frame_select_next(frame);
                ret = TRUE;
            }

            else if (sym == XK_Home) {
                menu_frame_select_first(frame);
                ret = TRUE;
            }

            else if (sym == XK_End) {
                menu_frame_select_last(frame);
                ret = TRUE;
            }

            /* keyboard accelerator shortcuts. (if it was a valid key) */
            else if (frame->entries &&
                     (unikey = obt_keyboard_keypress_to_unichar(ev)))
            {
                GList *start;
                GList *it;
                ObMenuEntryFrame *found = NULL;
                guint num_found = 0;

                /* menu shortcuts are not case sensitive */
                unikey = g_unichar_tolower(unikey);

                /* start after the selected one */
                start = frame->entries;
                if (frame->selected) {
                    for (it = start; frame->selected != it->data;
                         it = g_list_next(it))
                        g_assert(it != NULL); /* nothing was selected? */
                    /* next with wraparound */
                    start = g_list_next(it);
                    if (start == NULL) start = frame->entries;
                }

                it = start;
                do {
                    ObMenuEntryFrame *e = it->data;
                    gunichar entrykey = 0;

                    if (e->entry->type == OB_MENU_ENTRY_TYPE_NORMAL)
                        entrykey = e->entry->data.normal.shortcut;
                    else if (e->entry->type == OB_MENU_ENTRY_TYPE_SUBMENU)
                        entrykey = e->entry->data.submenu.submenu->shortcut;

                    if (unikey == entrykey) {
                        if (found == NULL) found = e;
                        ++num_found;
                    }

                    /* next with wraparound */
                    it = g_list_next(it);
                    if (it == NULL) it = frame->entries;
                } while (it != start);

                if (found) {
                    menu_frame_select(frame, found, TRUE);

                    if (num_found == 1) {
                        if (found->entry->type == OB_MENU_ENTRY_TYPE_SUBMENU) {
                            /* move focus to the child menu */
                            menu_frame_select_next(frame->child);
                        }
                        else {
                            frame->press_doexec = TRUE;
                        }
                    }
                    ret = TRUE;
                }
            }
        }

        /* Use KeyRelease events for running things so that the key release
           doesn't get sent to the focused application.

           Allow ControlMask only, and don't bother if the menu is empty */
        else if (ev->type == OB_EVENT_KEY_RELEASE && (mods & ~ctrl) == 0) {
            if (frame->press_keycode == ev->keycode &&
                frame->got_press &&
                frame->press_doexec)
            {
                if (frame->selected)
                    menu_entry_frame_execute(frame->selected, ev->state);
            }
        }
    }

    return ret;
}

/* Motion over the menus selects the entry under the pointer, this replaces
   the Enter/Leave/MotionNotify handling of Openbox's event_handle_menu().
   Moving from one entry to another is a LeaveNotify on the old entry's
   window followed by an EnterNotify on the new one, like in X. */
static void event_handle_menu(ObEvent *ev)
{
    static ObMenuEntryFrame *last = NULL;
    static ObMenuFrame *last_frame = NULL;
    ObMenuFrame *f;
    ObMenuEntryFrame *e;

    /* the entry the pointer was in is gone if its menu was hidden or
       updated since */
    if (last && !(g_list_find(menu_frame_visible, last_frame) &&
                  g_list_find(last_frame->entries, last)))
        last = NULL;

    e = menu_entry_frame_under(ev->x_root, ev->y_root);

    if (last && last != e && (!e || e->frame != last_frame))
        /* LeaveNotify: the pointer didn't move to another entry of the
           menu, select nothing in the menu */
        menu_frame_select(last_frame, NULL, FALSE);

    if (e && e != last && e->ignore_enters)
        /* EnterNotify */
        --e->ignore_enters;
    else if (e) {
        if (!(f = find_active_menu()) ||
            f == e->frame ||
            f->parent == e->frame ||
            f->child == e->frame)
            menu_frame_select(e->frame, e, FALSE);
    }

    last = e;
    last_frame = e ? e->frame : NULL;
}

static gboolean event_handle_user_input(ObClient *client, ObEvent *e)
{
    g_assert(e->type == OB_EVENT_BUTTON_PRESS ||
             e->type == OB_EVENT_BUTTON_RELEASE ||
             e->type == OB_EVENT_MOTION ||
             e->type == OB_EVENT_KEY_PRESS ||
             e->type == OB_EVENT_KEY_RELEASE);

    if (menu_frame_visible) {
        if (e->type == OB_EVENT_MOTION)
            return TRUE;
        if (event_handle_menu_input(e))
            /* don't use the event if the menu used it, but if the menu
               didn't use it and it's a keypress that is bound, it will
               close the menu and be used */
            return TRUE;
    }

    /* if the keyboard interactive action uses the event then dont
       use it for bindings. likewise is moveresize uses the event. */
    if (actions_interactive_input_event(e) || moveresize_event(e))
        return TRUE;

    if (moveresize_in_progress)
        /* make further actions work on the client being
           moved/resized */
        client = moveresize_client;

    if (e->type == OB_EVENT_BUTTON_PRESS ||
        e->type == OB_EVENT_BUTTON_RELEASE ||
        e->type == OB_EVENT_MOTION)
    {
        /* the frame may not be "visible" but they can still click on it
           in the case where it is animating before disappearing */
        if (!client || !frame_iconify_animating(client->frame))
            return mouse_event(client, e);
    } else
        return keyboard_event((focus_cycle_target ? focus_cycle_target :
                               (client ? client : focus_client)), e);

    return FALSE;
}

static void focus_delay_dest(gpointer data)
{
    g_slice_free(ObFocusDelayData, data);
    focus_delay_timeout_id = 0;
    focus_delay_timeout_client = NULL;
}

static void unfocus_delay_dest(gpointer data)
{
    g_slice_free(ObFocusDelayData, data);
    unfocus_delay_timeout_id = 0;
    unfocus_delay_timeout_client = NULL;
}

static gboolean focus_delay_func(gpointer data)
{
    ObFocusDelayData *d = data;
    guint32 old = event_curtime; /* save the curtime */

    event_curtime = d->time;
    if (client_focus(d->client) && config_focus_raise)
        stacking_raise(CLIENT_AS_WINDOW(d->client));
    event_curtime = old;

    return FALSE; /* no repeat */
}

static gboolean unfocus_delay_func(gpointer data)
{
    ObFocusDelayData *d = data;
    guint32 old = event_curtime; /* save the curtime */

    event_curtime = d->time;
    focus_nothing();
    event_curtime = old;

    return FALSE; /* no repeat */
}

static void focus_delay_client_dest(ObClient *client, gpointer data)
{
    if (focus_delay_timeout_client == client && focus_delay_timeout_id)
        g_source_remove(focus_delay_timeout_id);
    if (unfocus_delay_timeout_client == client && unfocus_delay_timeout_id)
        g_source_remove(unfocus_delay_timeout_id);
}

void event_halt_focus_delay(void)
{
    if (focus_delay_timeout_id) g_source_remove(focus_delay_timeout_id);
    if (unfocus_delay_timeout_id) g_source_remove(unfocus_delay_timeout_id);
}

gulong event_start_ignore_all_enters(void)
{
    ++ignore_enters;
    return 1;
}

void event_end_ignore_all_enters(gulong start)
{
    if (ignore_enters > 0)
        --ignore_enters;
    /* windows may have moved under the pointer, the client under it gets
       the pointer focus but this is not an enter that moves the focus */
    if (ignore_enters == 0 && ob_state() == OB_STATE_RUNNING)
        seat_rebase_pointer();
}

gboolean event_ignoring_enters(void)
{
    return ignore_enters > 0;
}

void event_cancel_all_key_grabs(void)
{
    if (actions_interactive_act_running()) {
        actions_interactive_cancel_act();
        ob_debug("KILLED interactive action");
    }
    else if (menu_frame_visible) {
        menu_frame_hide_all();
        ob_debug("KILLED open menus");
    }
    else if (moveresize_in_progress) {
        moveresize_end(TRUE);
        ob_debug("KILLED interactive moveresize");
    }
    else if (grab_on_keyboard()) {
        ungrab_keyboard();
        ob_debug("KILLED active grab on keyboard");
    }
    else
        ungrab_passive_key();
}

gboolean event_time_after(guint32 t1, guint32 t2)
{
    /*
      Timestamp values wrap around (after about 49.7 days). The server, given
      its current time is represented by timestamp T, always interprets
      timestamps from clients by treating half of the timestamp space as being
      later in time than T.
      - http://tronche.com/gui/x/xlib/input/pointer-grabbing.html
    */

    /* TIME_HALF is not half of the number space of a Time type variable.
     * Rather, it is half the number space of a timestamp value, which is
     * always 32 bits. */
#define TIME_HALF (guint32)(1 << 31)

    if (t2 >= TIME_HALF)
        /* t2 is in the second half so t1 might wrap around and be smaller than
           t2 */
        return t1 >= t2 || t1 < (t2 + TIME_HALF);
    else
        /* t2 is in the first half so t1 has to come after it */
        return t1 >= t2 && t1 < (t2 + TIME_HALF);
}

guint32 event_time(void)
{
    if (event_curtime) return event_curtime;
    return server_now_msec();
}

guint32 event_source_time(void)
{
    return event_sourcetime;
}

void event_reset_time(void)
{
    event_curtime = 0;
}

void event_update_user_time(void)
{
    event_last_user_time = event_time();
}

void event_reset_user_time(void)
{
    event_last_user_time = 0;
}
