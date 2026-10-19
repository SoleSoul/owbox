/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   app_menu.c for owbox
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

/* The "applications-menu", the installed applications (their desktop
   entries) by category.  Openbox users get this from pipe menus like
   obmenu-generator or xdg-menu, this one is built in and follows newly
   installed applications.  Use it in menu.xml with
     <menu id="applications-menu"/> */

#include "app_menu.h"
#include "openbox.h"
#include "menu.h"
#include "menuframe.h"
#include "actions.h"
#include "config.h"
#include "icons.h"
#include "gettext.h"

#include <sfdo-desktop.h>
#include <libxml/parser.h>
#include <string.h>

#define MENU_NAME "applications-menu"

/* the main categories of the freedesktop.org menu specification */
static const struct {
    const gchar *category;
    const gchar *label;
    const gchar *icon;
} categories[] = {
    { "AudioVideo",  N_("Multimedia"),  "applications-multimedia" },
    { "Development", N_("Development"), "applications-development" },
    { "Education",   N_("Education"),   "applications-education" },
    { "Game",        N_("Games"),       "applications-games" },
    { "Graphics",    N_("Graphics"),    "applications-graphics" },
    { "Network",     N_("Internet"),    "applications-internet" },
    { "Office",      N_("Office"),      "applications-office" },
    { "Science",     N_("Science"),     "applications-science" },
    { "Settings",    N_("Settings"),    "preferences-desktop" },
    { "System",      N_("System"),      "applications-system" },
    { "Utility",     N_("Accessories"), "applications-utilities" },
    { NULL,          N_("Other"),       "applications-other" },
};
#define N_CATEGORIES G_N_ELEMENTS(categories)

static ObMenu *app_menu;
static ObMenu *category_menus[N_CATEGORIES];
/*! Icons by name, so opening the menu doesn't load them again */
static GHashTable *icon_cache;

static gboolean shown_here(struct sfdo_desktop_entry *e)
{
    const gchar *cur = g_getenv("XDG_CURRENT_DESKTOP");
    gchar **names;
    gboolean show = FALSE;
    gint i;

    if (!cur || !*cur)
        return sfdo_desktop_entry_show_in(e, NULL, 0);
    names = g_strsplit(cur, ":", -1);
    for (i = 0; names[i] && !show; ++i)
        show = sfdo_desktop_entry_show_in(e, names[i], SFDO_NT);
    g_strfreev(names);
    return show;
}

/*! Which of the categories an application goes in, or -1 if it isn't
  shown */
static gint entry_category(struct sfdo_desktop_entry *e)
{
    const struct sfdo_string *cats;
    const gchar *try_exec;
    size_t n, i;
    guint c;

    if (sfdo_desktop_entry_get_type(e) != SFDO_DESKTOP_ENTRY_APPLICATION ||
        sfdo_desktop_entry_get_no_display(e) ||
        !sfdo_desktop_entry_get_exec(e) ||
        !shown_here(e))
        return -1;
    if ((try_exec = sfdo_desktop_entry_get_try_exec(e, NULL))) {
        gchar *path = g_path_is_absolute(try_exec) ?
            (g_file_test(try_exec, G_FILE_TEST_IS_EXECUTABLE) ?
             g_strdup(try_exec) : NULL) :
            g_find_program_in_path(try_exec);
        if (!path) return -1;
        g_free(path);
    }

    /* the first main category it lists, so each application is in the
       menu once */
    cats = sfdo_desktop_entry_get_categories(e, &n);
    for (i = 0; i < n; ++i)
        for (c = 0; categories[c].category; ++c)
            if (!strcmp(cats[i].data, categories[c].category))
                return c;
    return N_CATEGORIES - 1;
}

static RrImage* cached_icon(const gchar *name)
{
    RrImage *img;

    if (!name || !*name || !config_menu_show_icons) return NULL;
    if (g_hash_table_lookup_extended(icon_cache, name, NULL,
                                     (gpointer*)&img))
        return img;
    img = icons_load(name, 48);
    g_hash_table_insert(icon_cache, g_strdup(name), img);
    return img;
}

static void set_icon(ObMenuEntry *e, RrImage *img, gboolean submenu)
{
    if (!img) return;
    RrImageRef(img);
    if (submenu) {
        e->data.submenu.icon = img;
        e->data.submenu.icon_alpha = 0xff;
    } else {
        e->data.normal.icon = img;
        e->data.normal.icon_alpha = 0xff;
    }
}

/*! An Execute action that runs the application, the same as one written
  in menu.xml, with startup notification */
static ObActionsAct* execute_action(struct sfdo_desktop_entry *e)
{
    struct sfdo_desktop_exec_command *cmd;
    const gchar **args;
    const gchar *s;
    GString *command = g_string_new(NULL), *xml = g_string_new(NULL);
    gchar *esc;
    xmlDocPtr doc;
    ObActionsAct *act = NULL;
    size_t i;

    cmd = sfdo_desktop_exec_format(sfdo_desktop_entry_get_exec(e), NULL);
    if (!cmd) goto done;
    args = sfdo_desktop_exec_command_get_args(cmd, NULL);

    /* Execute doesn't change directory */
    if ((s = sfdo_desktop_entry_get_path(e, NULL)) && *s) {
        gchar *q = g_shell_quote(s);
        g_string_append_printf(command,
                               "sh -c 'cd \"$0\" && exec \"$@\"' %s ", q);
        g_free(q);
    }
    if (sfdo_desktop_entry_get_terminal(e))
        g_string_append(command, "owbox-terminal -e ");
    for (i = 0; args[i]; ++i) {
        gchar *q = g_shell_quote(args[i]);
        if (i) g_string_append_c(command, ' ');
        g_string_append(command, q);
        g_free(q);
    }
    sfdo_desktop_exec_command_destroy(cmd);

    esc = g_markup_escape_text(command->str, -1);
    g_string_append_printf(xml, "<action name=\"Execute\"><command>%s"
                           "</command>", esc);
    g_free(esc);
    g_string_append_printf(xml, "<startupnotify><enabled>%s</enabled>",
                           sfdo_desktop_entry_get_startup_notify(e) ==
                           SFDO_DESKTOP_ENTRY_STARTUP_NOTIFY_FALSE ?
                           "no" : "yes");
    esc = g_markup_escape_text(sfdo_desktop_entry_get_name(e, NULL), -1);
    g_string_append_printf(xml, "<name>%s</name>", esc);
    g_free(esc);
    if ((s = sfdo_desktop_entry_get_icon(e, NULL))) {
        esc = g_markup_escape_text(s, -1);
        g_string_append_printf(xml, "<icon>%s</icon>", esc);
        g_free(esc);
    }
    if ((s = sfdo_desktop_entry_get_startup_wm_class(e, NULL))) {
        esc = g_markup_escape_text(s, -1);
        g_string_append_printf(xml, "<wmclass>%s</wmclass>", esc);
        g_free(esc);
    }
    g_string_append(xml, "</startupnotify></action>");

    doc = xmlReadMemory(xml->str, xml->len, "app_menu.xml", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (doc) {
        act = actions_parse(xmlDocGetRootElement(doc));
        xmlFreeDoc(doc);
    }

done:
    g_string_free(command, TRUE);
    g_string_free(xml, TRUE);
    return act;
}

static gboolean category_update(ObMenuFrame *frame, gpointer data)
{
    ObMenu *menu = frame->menu;
    gint cat = GPOINTER_TO_INT(data);
    struct sfdo_desktop_db *db = icons_desktop_db();
    struct sfdo_desktop_entry **entries = NULL;
    size_t n = 0, i;

    menu_clear_entries(menu);
    if (db) entries = sfdo_desktop_db_get_entries(db, &n);
    for (i = 0; i < n; ++i) {
        ObActionsAct *act;
        ObMenuEntry *e;

        if (entry_category(entries[i]) != cat) continue;
        if (!(act = execute_action(entries[i]))) continue;
        e = menu_add_normal(menu, -1,
                            sfdo_desktop_entry_get_name(entries[i], NULL),
                            g_slist_prepend(NULL, act), FALSE);
        set_icon(e, cached_icon(sfdo_desktop_entry_get_icon(entries[i],
                                                            NULL)),
                 FALSE);
    }
    menu_sort_entries(menu);
    return TRUE;
}

static gboolean self_update(ObMenuFrame *frame, gpointer data)
{
    ObMenu *menu = frame->menu;
    struct sfdo_desktop_db *db = icons_desktop_db();
    struct sfdo_desktop_entry **entries = NULL;
    gboolean used[N_CATEGORIES] = { FALSE };
    size_t n = 0, i;
    guint c;

    menu_clear_entries(menu);
    if (db) entries = sfdo_desktop_db_get_entries(db, &n);
    for (i = 0; i < n; ++i) {
        gint cat = entry_category(entries[i]);
        if (cat >= 0) used[cat] = TRUE;
    }
    /* the categories that have applications in them */
    for (c = 0; c < N_CATEGORIES; ++c)
        if (used[c]) {
            ObMenuEntry *e = menu_add_submenu(menu, -1,
                                              category_menus[c]->name);
            set_icon(e, cached_icon(categories[c].icon), TRUE);
        }
    return TRUE;
}

static void self_cleanup(ObMenu *menu, gpointer data)
{
    menu_clear_entries(menu);
}

void app_menu_startup(gboolean reconfig)
{
    guint c;

    icon_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify)RrImageUnref);

    for (c = 0; c < N_CATEGORIES; ++c) {
        gchar *name = g_strdup_printf("%s-%s", MENU_NAME,
                                      categories[c].category ?
                                      categories[c].category : "Other");
        category_menus[c] = menu_new(name, _(categories[c].label), FALSE,
                                     GINT_TO_POINTER(c));
        menu_set_update_func(category_menus[c], category_update);
        menu_set_cleanup_func(category_menus[c], self_cleanup);
        g_free(name);
    }

    app_menu = menu_new(MENU_NAME, _("Applications"), TRUE, NULL);
    menu_set_update_func(app_menu, self_update);
    menu_set_cleanup_func(app_menu, self_cleanup);
}

void app_menu_shutdown(gboolean reconfig)
{
    /* the menus are destroyed with all the others */
    g_hash_table_destroy(icon_cache);
    icon_cache = NULL;
}
