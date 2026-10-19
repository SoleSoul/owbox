/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   obt/keyboard.h for owbox
   Copyright (c) 2007        Dana Jansens
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

/* Openbox's obt keyboard module maps X modifier keys to modifier masks.  With
   Wayland the modifiers come from xkbcommon through wlroots, this keeps the
   same interface for the Openbox code that uses it. */

#ifndef __obt_keyboard_h
#define __obt_keyboard_h

#include <glib.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

struct _ObEvent;

/*! Openbox's X input context, unused (the events carry their text) */
typedef struct _ObtIC ObtIC;

typedef enum {
    OBT_KEYBOARD_MODKEY_NONE,
    OBT_KEYBOARD_MODKEY_CAPSLOCK,
    OBT_KEYBOARD_MODKEY_NUMLOCK,
    OBT_KEYBOARD_MODKEY_SCROLLLOCK,
    OBT_KEYBOARD_MODKEY_SHIFT,
    OBT_KEYBOARD_MODKEY_CONTROL,
    OBT_KEYBOARD_MODKEY_SUPER,
    OBT_KEYBOARD_MODKEY_HYPER,
    OBT_KEYBOARD_MODKEY_META,
    OBT_KEYBOARD_MODKEY_ALT,

    OBT_KEYBOARD_NUM_MODKEYS
} ObtModkeysKey;

/*! Get the modifier mask(s) for a KeyCode. (eg. a keycode bound to Alt_L could
  return a mask of (Mod1Mask | Mask3Mask)) */
guint obt_keyboard_keyevent_to_modmask(struct _ObEvent *e);

/*! Strip off all modifiers except for the modifier keys. This strips stuff
  like Button1Mask, and also LockMask, NumlockMask, and ScrolllockMask */
guint obt_keyboard_only_modmasks(guint mask);

/*! Get the modifier masks for a modifier key. This includes both the left and
  right keys when there are both. */
guint obt_keyboard_modkey_to_modmask(ObtModkeysKey key);

/*! Translate a KeyPress event to the KeySym that it represents.  Use this
 for control keys, not for getting text input! */
xkb_keysym_t obt_keyboard_keypress_to_keysym(struct _ObEvent *e);

/*! Translate a KeyPress event to the unicode character it represents */
gunichar obt_keyboard_keypress_to_unichar(struct _ObEvent *e);

/* The X keysym names used by the Openbox code */
#define XK_Escape    XKB_KEY_Escape
#define XK_Return    XKB_KEY_Return
#define XK_KP_Enter  XKB_KEY_KP_Enter
#define XK_Tab       XKB_KEY_Tab
#define XK_space     XKB_KEY_space
#define XK_Left      XKB_KEY_Left
#define XK_Right     XKB_KEY_Right
#define XK_Up        XKB_KEY_Up
#define XK_Down      XKB_KEY_Down
#define XK_Home      XKB_KEY_Home
#define XK_End       XKB_KEY_End

#endif /* __obt_keyboard_h */
