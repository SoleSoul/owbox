/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   event.h for owbox (ported from the Openbox window manager)
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

#ifndef __events_h
#define __events_h

#include <glib.h>
#include <xkbcommon/xkbcommon.h>

struct _ObClient;
struct _ObFWin;
struct _ObMenuFrame;

/*! The amount of time before a window appears that is checked for user input
    to determine if the user is working in another window */
#define OB_EVENT_USER_TIME_DELAY (1000) /* milliseconds */

/*! Button masks in ObEvent::state, above the wlr modifier bits like X's
  Button1Mask..Button5Mask */
#define OB_BUTTON1_MASK (1 << 8)
#define OB_BUTTON2_MASK (1 << 9)
#define OB_BUTTON3_MASK (1 << 10)
#define OB_BUTTON4_MASK (1 << 11)
#define OB_BUTTON5_MASK (1 << 12)

typedef enum {
    OB_EVENT_KEY_PRESS = 1,
    OB_EVENT_KEY_RELEASE,
    OB_EVENT_BUTTON_PRESS,
    OB_EVENT_BUTTON_RELEASE,
    OB_EVENT_MOTION
} ObEventType;

/*! An input event, in place of the X events that Openbox's input handling
  (keyboard.c, mouse.c, interactive actions, menus) works with. */
typedef struct _ObEvent {
    ObEventType type;
    /*! Milliseconds */
    guint32 time;
    /*! The modifiers (wlr_keyboard_modifier) and buttons (OB_BUTTON*_MASK)
      held before the event, like an X event's state */
    guint state;

    /*! Key events: the xkb keycode */
    xkb_keycode_t keycode;
    /*! Key events: the keysym with the modifiers applied */
    xkb_keysym_t sym;
    /*! Key events: the keysym of the key at shift level 0 */
    xkb_keysym_t raw_sym;
    /*! Key events: the keysym of the key at shift level 0 in the first
      keyboard layout, which bindings also match like Openbox's do (they
      are keycodes from the first layout's keysyms) */
    xkb_keysym_t first_sym;
    /*! Key events: the modifiers that were used up to produce sym */
    guint consumed_mods;
    /*! Key events: the modifier mask of the key, if it is a modifier key */
    guint modmask;
    /*! Key press: the text the key types, if any */
    gunichar unichar;

    /*! Button events: X numbering, 1 left, 2 middle, 3 right, 4-7 scroll */
    guint button;

    /*! The pointer position in the layout */
    gint x_root, y_root;
    /*! The pointer position relative to the frame element (or client) */
    gint x, y;
    /*! The client the event is for, NULL if none */
    struct _ObClient *client;
    /*! The frame element the pointer is on, NULL if it is on the client
      itself or on no client */
    struct _ObFWin *win;
    /*! The scene node descriptor the pointer is on, NULL for the root */
    struct _ObNode *node;
} ObEvent;

/*! The last user-interaction time, as given by the clients */
extern guint32 event_last_user_time;

void event_startup(gboolean reconfig);
void event_shutdown(gboolean reconfig);

/* Called by seat.c with the input it gets */

/*! A key was pressed or released.  Returns TRUE if owbox used the key and
  it should not go to the focused client. */
gboolean event_key(ObEvent *e);
/*! A button was pressed or released, or the pointer moved.  Returns TRUE if
  owbox used the event and it should not go to the client. */
gboolean event_pointer(ObEvent *e);
/*! The pointer entered a new client (or none) through motion (motion is
  TRUE) or because windows moved under it */
void event_pointer_enter(struct _ObClient *client, gboolean motion);
/*! Keyboard focus moved from one surface to another */
void event_focus_changed(struct _ObClient *old, struct _ObClient *new_,
                         gboolean to_other_surface);

/*! Make as if the mouse just entered the client, use only when using focus
    follows mouse */
void event_enter_client(struct _ObClient *client);

/*! Make as if the mouse just left the client, use only when using focus
    follows mouse */
void event_leave_client(struct _ObClient *client);

/*! Make mouse focus not move at all from the stuff that happens between these
    two function calls. */
gulong event_start_ignore_all_enters(void);
void event_end_ignore_all_enters(gulong start);
/*! Are enter events being ignored right now */
gboolean event_ignoring_enters(void);

/*! End *all* active and passive grabs on the keyboard
    This is called in situations where if there is a grab going on, then
    we need to cancel it. If we move focus during the grab, applications
    will get NotifyWhileGrabbed events and ignore them!

    Actions should not rely on being able to move focus during an
    interactive grab. */
void event_cancel_all_key_grabs(void);

/* Halts any focus delay in progress, use this when the user is selecting a
   window for focus */
void event_halt_focus_delay(void);

/*! Compare t1 and t2, taking into account wraparound. True if t1
    comes at the same time or later than t2. */
gboolean event_time_after(guint32 t1, guint32 t2);

/*! Time at which the current event occured.  If this is not known, this
  is a time at or after it, but at or before any other events we will process
*/
guint32 event_time(void);

/*! Force event_time() to skip the current timestamp and look for the next
  one. */
void event_reset_time(void);

/*! A time at which an event happened that caused this current event to be
  generated.  This is a user-provided time and not to be trusted.
  Returns 0 if there was no source time provided.
*/
guint32 event_source_time(void);

/*! Update the timestamp for when the user has last used the focused window.
  This updates the timestamp to the time of the last event, given by
  event_time().
*/
void event_update_user_time(void);

/*! Reset the timestamp for when the user has last used the focused window. */
void event_reset_user_time(void);

#endif
