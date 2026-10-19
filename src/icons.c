/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   icons.c for owbox
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

#include "icons.h"
#include "client.h"
#include "config.h"
#include "openbox.h"
#include "debug.h"

#include <sfdo-basedir.h>
#include <sfdo-desktop.h>
#include <sfdo-icon.h>
#include <string.h>

static struct sfdo_basedir_ctx *basedir = NULL;
static struct sfdo_desktop_ctx *desktop_ctx = NULL;
static struct sfdo_desktop_db *desktop_db = NULL;
static struct sfdo_icon_ctx *icon_ctx = NULL;
static struct sfdo_icon_theme *icon_theme = NULL;

void icons_startup(gboolean reconfig)
{
    basedir = sfdo_basedir_ctx_create();
    desktop_ctx = sfdo_desktop_ctx_create(basedir);
    if (desktop_ctx)
        desktop_db = sfdo_desktop_db_load(desktop_ctx, NULL);
    icon_ctx = sfdo_icon_ctx_create(basedir);
    if (icon_ctx)
        icon_theme = sfdo_icon_theme_load(icon_ctx, config_icon_theme,
            SFDO_ICON_THEME_LOAD_OPTION_ALLOW_MISSING |
            SFDO_ICON_THEME_LOAD_OPTION_RELAXED);
    if (!icon_theme)
        ob_debug("Unable to load the icon theme");
}

void icons_shutdown(gboolean reconfig)
{
    sfdo_icon_theme_destroy(icon_theme);
    sfdo_icon_ctx_destroy(icon_ctx);
    sfdo_desktop_db_destroy(desktop_db);
    sfdo_desktop_ctx_destroy(desktop_ctx);
    sfdo_basedir_ctx_destroy(basedir);
    icon_theme = NULL;
    icon_ctx = NULL;
    desktop_db = NULL;
    desktop_ctx = NULL;
    basedir = NULL;
}

RrImage* icons_load(const gchar *name, gint size)
{
    struct sfdo_icon_file *file;
    RrImage *img = NULL;

    if (!name || !*name) return NULL;

    if (name[0] == '/' || name[0] == '~')
        /* Openbox's menu icons are file names */
        return RrImageNewFromName(ob_rr_icons, name);

    if (!icon_theme) return NULL;
    file = sfdo_icon_theme_lookup(icon_theme, name, SFDO_NT, size, 1,
                                  SFDO_ICON_THEME_LOOKUP_OPTIONS_DEFAULT);
    if (file && file != SFDO_ICON_FILE_INVALID) {
        if (sfdo_icon_file_get_format(file) != SFDO_ICON_FILE_FORMAT_XPM)
            img = RrImageNewFromName(ob_rr_icons,
                                     sfdo_icon_file_get_path(file, NULL));
    }
    sfdo_icon_file_destroy(file);
    return img;
}

static struct sfdo_desktop_entry* find_entry(const gchar *app_id)
{
    struct sfdo_desktop_entry *e, **entries;
    size_t n, i;
    gchar *lower;

    if (!desktop_db || !app_id || !*app_id) return NULL;

    if ((e = sfdo_desktop_db_get_entry_by_id(desktop_db, app_id, SFDO_NT)))
        return e;
    lower = g_ascii_strdown(app_id, -1);
    e = sfdo_desktop_db_get_entry_by_id(desktop_db, lower, SFDO_NT);
    g_free(lower);
    if (e) return e;

    /* look for an entry whose StartupWMClass or id ends with the app id,
       (org.example.Foo for app id foo) */
    entries = sfdo_desktop_db_get_entries(desktop_db, &n);
    for (i = 0; i < n; ++i) {
        const gchar *wmclass =
            sfdo_desktop_entry_get_startup_wm_class(entries[i], NULL);
        const gchar *id = sfdo_desktop_entry_get_id(entries[i], NULL);
        gsize il = strlen(id), al = strlen(app_id);
        if (wmclass && !g_ascii_strcasecmp(wmclass, app_id))
            return entries[i];
        if (il > al && id[il - al - 1] == '.' &&
            !g_ascii_strcasecmp(id + il - al, app_id))
            return entries[i];
    }
    return NULL;
}

RrImage* icons_for_client(ObClient *c)
{
    struct sfdo_desktop_entry *e;
    const gchar *icon = NULL;
    RrImage *img;

    if ((e = find_entry(c->class)))
        icon = sfdo_desktop_entry_get_icon(e, NULL);
    if ((img = icons_load(icon, 64)))
        return img;
    /* some apps name their icon after their app id */
    if (c->class && *c->class)
        return icons_load(c->class, 64);
    return NULL;
}
