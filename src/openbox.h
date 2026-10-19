/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   openbox.h for owbox (ported from the Openbox window manager)
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

#ifndef __openbox_h
#define __openbox_h

#include "misc.h"

#include "render/render.h"
#include "render/theme.h"

#include <glib.h>

extern RrInstance *ob_rr_inst;
extern RrImageCache *ob_rr_icons;

/*! The config directories' subdirectories looked in for rc.xml and the
  menus, ~/.config/owbox before ~/.config/openbox, and so on */
extern const gchar *const ob_config_domains[];
extern RrTheme    *ob_rr_theme;

extern GMainLoop *ob_main_loop;

/*! The number of the screen on which we're running, always 0 */
extern gint     ob_screen;

extern gboolean ob_replace_wm;
extern gboolean ob_debug_xinerama;

/*! The current locale for the LC_MESSAGES category */
extern const gchar *ob_locale_msg;

/* The state of execution of the window manager */
ObState ob_state(void);
void ob_set_state(ObState state);

void ob_restart_other(const gchar *path);
void ob_restart(void);
void ob_exit(gint code);
void ob_exit_replace(void);

void ob_reconfigure(void);

void ob_exit_with_error(const gchar *msg) G_GNUC_NORETURN;

/*! The path of the configuration file in use, or NULL */
const gchar* ob_config_file(void);

#endif
