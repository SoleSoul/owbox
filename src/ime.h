/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ime.h for owbox
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

#ifndef __ime_h
#define __ime_h

#include <glib.h>

struct wlr_keyboard;
struct wlr_keyboard_key_event;

/* Input methods (fcitx5): the input-method-v2 protocol for the
   input method, text-input-v3 for applications.  owbox relays between the
   focused window's text field and the input method, sends it the keys while
   it asks for them, and places its candidate popup by the text cursor. */

void ime_init(void);
void ime_finish(void);

/*! A key, or the modifiers, that owbox doesn't use.  Returns TRUE if they
  went to the input method, rather than to the focused client */
gboolean ime_key(struct wlr_keyboard *keyboard,
                 struct wlr_keyboard_key_event *ev);
gboolean ime_modifiers(struct wlr_keyboard *keyboard);

#endif
