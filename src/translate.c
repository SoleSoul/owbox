/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   translate.c for owbox (ported from the Openbox window manager)
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

#include "openbox.h"
#include "mouse.h"
#include "translate.h"
#include "gettext.h"
#include "obt/keyboard.h"

#include <wlr/types/wlr_keyboard.h>

#include <glib.h>
#include <string.h>
#include <stdlib.h>

static guint translate_modifier(gchar *str)
{
    guint mask = 0;

    if (!g_ascii_strcasecmp("Mod1", str)) mask = WLR_MODIFIER_ALT;
    else if (!g_ascii_strcasecmp("Mod2", str)) mask = WLR_MODIFIER_MOD2;
    else if (!g_ascii_strcasecmp("Mod3", str)) mask = WLR_MODIFIER_MOD3;
    else if (!g_ascii_strcasecmp("Mod4", str)) mask = WLR_MODIFIER_LOGO;
    else if (!g_ascii_strcasecmp("Mod5", str)) mask = WLR_MODIFIER_MOD5;

    else if (!g_ascii_strcasecmp("Control", str) ||
             !g_ascii_strcasecmp("C", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_CONTROL);
    else if (!g_ascii_strcasecmp("Alt", str) ||
             !g_ascii_strcasecmp("A", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_ALT);
    else if (!g_ascii_strcasecmp("Meta", str) ||
             !g_ascii_strcasecmp("M", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_META);
    /* W = windows key, is linked to the Super_L/R buttons */
    else if (!g_ascii_strcasecmp("Super", str) ||
             !g_ascii_strcasecmp("W", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_SUPER);
    else if (!g_ascii_strcasecmp("Shift", str) ||
             !g_ascii_strcasecmp("S", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_SHIFT);
    else if (!g_ascii_strcasecmp("Hyper", str) ||
             !g_ascii_strcasecmp("H", str))
        mask = obt_keyboard_modkey_to_modmask(OBT_KEYBOARD_MODKEY_HYPER);
    else
        g_message(_("Invalid modifier key \"%s\" in key/mouse binding"), str);

    return mask;
}

gboolean translate_button(const gchar *str, guint *state, guint *button)
{
    gchar **parsed;
    gchar *l;
    gint i;
    gboolean ret = FALSE;

    parsed = g_strsplit(str, "-", -1);

    /* first, find the button (last token) */
    l = NULL;
    for (i = 0; parsed[i] != NULL; ++i)
        l = parsed[i];
    if (l == NULL)
        goto translation_fail;

    /* figure out the mod mask */
    *state = 0;
    for (i = 0; parsed[i] != l; ++i) {
        guint m = translate_modifier(parsed[i]);
        if (!m) goto translation_fail;
        *state |= m;
    }

    /* figure out the button */
    if (!g_ascii_strcasecmp("Left", l)) *button = 1;
    else if (!g_ascii_strcasecmp("Middle", l)) *button = 2;
    else if (!g_ascii_strcasecmp("Right", l)) *button = 3;
    else if (!g_ascii_strcasecmp("Up", l)) *button = 4;
    else if (!g_ascii_strcasecmp("Down", l)) *button = 5;
    else if (!g_ascii_strncasecmp("Button", l, 6)) *button = atoi(l+6);
    if (!*button)
        goto translation_fail;

    ret = TRUE;

translation_fail:
    g_strfreev(parsed);
    return ret;
}

gboolean translate_key(const gchar *str, guint *state, guint *key)
{
    gchar **parsed;
    gchar *l;
    gint i;
    gboolean ret = FALSE;
    xkb_keysym_t sym;

    parsed = g_strsplit(str, "-", -1);

    *state = *key = 0;

    /* first, find the key (last token) */
    l = NULL;
    for (i = 0; parsed[i] != NULL; ++i)
        l = parsed[i];
    if (l == NULL)
        goto translation_fail;

    /* figure out the mod mask */
    *state = 0;
    for (i = 0; parsed[i] != l; ++i) {
        guint m = translate_modifier(parsed[i]);
        if (!m) goto translation_fail;
        *state |= m;
    }

    if (!g_ascii_strncasecmp("0x", l, 2)) {
        gchar *end;

        /* take it directly, it is an X keycode, which is an xkb keycode */
        *key = strtol(l, &end, 16);
        if (*l == '\0' || *end != '\0' || *key == 0) {
            g_message(_("Invalid key code \"%s\" in key binding"), l);
            goto translation_fail;
        }
        *key |= OB_KEY_IS_KEYCODE;
    } else {
        /* figure out the keysym, X's key names are xkb's key names */
        sym = xkb_keysym_from_name(l, XKB_KEYSYM_NO_FLAGS);
        if (sym == XKB_KEY_NoSymbol)
            sym = xkb_keysym_from_name(l, XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == XKB_KEY_NoSymbol) {
            g_message(_("Invalid key name \"%s\" in key binding"), l);
            goto translation_fail;
        }
        /* bindings are matched against the unshifted key, like Openbox
           matches the keycode, so S-a and S-A are the same */
        *key = xkb_keysym_to_lower(sym);
    }

    ret = TRUE;

translation_fail:
    g_strfreev(parsed);
    return ret;
}

