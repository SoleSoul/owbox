/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   obt/keyboard.c for owbox
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

#include "obt/keyboard.h"
#include "event.h"

#include <wlr/types/wlr_keyboard.h>

guint obt_keyboard_keyevent_to_modmask(struct _ObEvent *e)
{
    return e->modmask;
}

guint obt_keyboard_only_modmasks(guint mask)
{
    /* strip caps lock (LOCK) and num lock (MOD2) */
    return mask & (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT |
                   WLR_MODIFIER_MOD3 | WLR_MODIFIER_LOGO | WLR_MODIFIER_MOD5);
}

guint obt_keyboard_modkey_to_modmask(ObtModkeysKey key)
{
    /* the modifier mapping of xkeyboard-config's default keymap */
    switch (key) {
    case OBT_KEYBOARD_MODKEY_CAPSLOCK: return WLR_MODIFIER_CAPS;
    case OBT_KEYBOARD_MODKEY_NUMLOCK: return WLR_MODIFIER_MOD2;
    case OBT_KEYBOARD_MODKEY_SCROLLLOCK: return WLR_MODIFIER_MOD3;
    case OBT_KEYBOARD_MODKEY_SHIFT: return WLR_MODIFIER_SHIFT;
    case OBT_KEYBOARD_MODKEY_CONTROL: return WLR_MODIFIER_CTRL;
    case OBT_KEYBOARD_MODKEY_SUPER: return WLR_MODIFIER_LOGO;
    case OBT_KEYBOARD_MODKEY_HYPER: return WLR_MODIFIER_LOGO;
    case OBT_KEYBOARD_MODKEY_META: return WLR_MODIFIER_ALT;
    case OBT_KEYBOARD_MODKEY_ALT: return WLR_MODIFIER_ALT;
    case OBT_KEYBOARD_MODKEY_NONE:
    case OBT_KEYBOARD_NUM_MODKEYS:
        break;
    }
    return 0;
}

xkb_keysym_t obt_keyboard_keypress_to_keysym(struct _ObEvent *e)
{
    return e->sym;
}

gunichar obt_keyboard_keypress_to_unichar(struct _ObEvent *e)
{
    return e->unichar;
}
