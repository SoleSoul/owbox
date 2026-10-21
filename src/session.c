/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   session.c for owbox
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

#include "session.h"
#include "debug.h"
#include "server.h"
#include "obt/paths.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* owbox is the session (on the hardware), and started its systemd target */
static gboolean in_session = FALSE;

static void spawn(gchar **argv)
{
    GError *e = NULL;

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &e))
    {
        g_message("Unable to run %s: %s", argv[0], e->message);
        g_error_free(e);
    }
}

/*! The programs owbox runs write their messages where owbox does, which is
  the console when owbox-session was run on one.  Send them, and owbox's own
  (they are in owbox.log too), to the journal, as a display manager does,
  so the console doesn't fill with them, nor with the clients' complaints
  when owbox exits. */
static void own_session(gpointer data)
{
    /* outlive the console's hangup, to log the clients' last messages */
    setsid();
}

static void output_to_journal(void)
{
    gchar *argv[] = { NULL, "--identifier=owbox", NULL };
    GError *e = NULL;
    gint in;

    if (!isatty(STDERR_FILENO) ||
        !(argv[0] = g_find_program_in_path("systemd-cat")))
        return;
    if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DEFAULT,
                                  own_session, NULL, NULL, &in, NULL, NULL,
                                  &e))
    {
        g_message("Unable to run %s: %s", argv[0], e->message);
        g_error_free(e);
        g_free(argv[0]);
        return;
    }
    g_free(argv[0]);
    fflush(stdout);
    fflush(stderr);
    dup2(in, STDOUT_FILENO);
    dup2(in, STDERR_FILENO);
    close(in);
}

void session_environment(void)
{
    gchar *exe;

    if (!g_getenv("XDG_CURRENT_DESKTOP"))
        g_setenv("XDG_CURRENT_DESKTOP", "owbox", TRUE);
    if (!g_getenv("XDG_SESSION_DESKTOP"))
        g_setenv("XDG_SESSION_DESKTOP", "owbox", TRUE);
    g_setenv("XDG_SESSION_TYPE", "wayland", TRUE);
    /* Java's toolkit needs this with window managers that don't reparent,
       which Xwayland's window manager doesn't */
    g_setenv("_JAVA_AWT_WM_NONREPARENTING", "1", FALSE);
    /* Qt applications look like Qt's defaults (light, hardly any icons)
       on desktops it doesn't know; its GTK theme makes them follow the
       desktop's settings (icon theme, light or dark, fonts), as under
       GNOME */
    g_setenv("QT_QPA_PLATFORMTHEME", "gtk3", FALSE);

    /* let D-Bus activated services (portals, notification daemons) and
       systemd user services find the session.  Not for an owbox running
       nested or headless, the services belong to the real session. */
    if (!server_is_session())
        return;
    output_to_journal();
    if ((exe = g_find_program_in_path("dbus-update-activation-environment")))
    {
        gchar *script = g_strdup_printf(
            "%s --systemd WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP "
            "XDG_SESSION_TYPE XDG_SESSION_DESKTOP XCURSOR_THEME XCURSOR_SIZE "
            "OWBOX_SOCK SSH_AUTH_SOCK QT_QPA_PLATFORMTHEME; "
            /* portals left running by an earlier session (the systemd user
               instance outlives it) serve that session's desktop, restart
               them for this one */
            "systemctl --user try-restart xdg-desktop-portal.service "
            "xdg-desktop-portal-wlr.service xdg-desktop-portal-gtk.service "
            ">/dev/null 2>&1; "
            /* graphical-session.target, for the services that come and go
               with the session.  Ones that failed before (when an earlier
               owbox crashed) start again. */
            "systemctl --user reset-failed >/dev/null 2>&1; "
            "systemctl --user start owbox-session.target >/dev/null 2>&1",
            exe);
        gchar *argv[] = { "/bin/sh", "-c", script, NULL };
        spawn(argv);
        g_free(script);
        g_free(exe);
        in_session = TRUE;
    }
}

/* ---- the autostart script ---- */

static void run_autostart_script(const gchar *path)
{
    gchar *argv[] = { "/bin/sh", (gchar*)path, NULL };

    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) return;
    ob_debug("Running autostart script %s", path);
    spawn(argv);
}

/* ---- XDG autostart (the freedesktop.org Desktop Application Autostart
   Specification), what openbox-xdg-autostart does ---- */

static gboolean desktop_matches(gchar **list)
{
    const gchar *cur = g_getenv("XDG_CURRENT_DESKTOP");
    gchar **mine, **it, **jt;
    gboolean found = FALSE;

    if (!cur) return FALSE;
    mine = g_strsplit(cur, ":", -1);
    for (it = list; *it && !found; ++it)
        for (jt = mine; *jt && !found; ++jt)
            if (!g_ascii_strcasecmp(*it, *jt))
                found = TRUE;
    g_strfreev(mine);
    return found;
}

/* Remove the field codes from an Exec line */
static gchar* strip_field_codes(const gchar *exec)
{
    GString *s = g_string_new(NULL);
    const gchar *p;

    for (p = exec; *p; ++p) {
        if (*p == '%' && p[1]) {
            if (p[1] == '%')
                g_string_append_c(s, '%');
            ++p;
            continue;
        }
        g_string_append_c(s, *p);
    }
    return g_string_free(s, FALSE);
}

static void run_autostart_entry(const gchar *path)
{
    GKeyFile *kf = g_key_file_new();
    const gchar *grp = G_KEY_FILE_DESKTOP_GROUP;
    gchar **list, *s, *exec;
    gsize n;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        goto done;

    if (g_key_file_get_boolean(kf, grp, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL))
        goto done;
    if ((list = g_key_file_get_string_list(
             kf, grp, G_KEY_FILE_DESKTOP_KEY_ONLY_SHOW_IN, &n, NULL)))
    {
        gboolean ok = desktop_matches(list);
        g_strfreev(list);
        if (!ok) goto done;
    }
    if ((list = g_key_file_get_string_list(
             kf, grp, G_KEY_FILE_DESKTOP_KEY_NOT_SHOW_IN, &n, NULL)))
    {
        gboolean ok = !desktop_matches(list);
        g_strfreev(list);
        if (!ok) goto done;
    }
    if ((s = g_key_file_get_string(kf, grp,
                                   G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL)))
    {
        gchar *found = g_path_is_absolute(s) ?
            (g_file_test(s, G_FILE_TEST_IS_EXECUTABLE) ? g_strdup(s) : NULL) :
            g_find_program_in_path(s);
        g_free(s);
        if (!found) goto done;
        g_free(found);
    }

    if ((s = g_key_file_get_string(kf, grp, G_KEY_FILE_DESKTOP_KEY_EXEC,
                                   NULL)))
    {
        gchar **argv = NULL;
        GError *e = NULL;

        exec = strip_field_codes(s);
        g_free(s);
        if (g_shell_parse_argv(exec, NULL, &argv, &e)) {
            ob_debug("Autostarting %s", path);
            spawn(argv);
            g_strfreev(argv);
        } else {
            g_message("Invalid Exec in %s: %s", path, e->message);
            g_error_free(e);
        }
        g_free(exec);
    }

done:
    g_key_file_unref(kf);
}

static void run_xdg_autostart(ObtPaths *paths)
{
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
    GSList *it;

    /* the autostart dirs are in order of importance, a file in an earlier
       directory replaces one with the same name in a later one */
    for (it = obt_paths_autostart_dirs(paths); it; it = g_slist_next(it)) {
        GDir *d = g_dir_open(it->data, 0, NULL);
        const gchar *name;

        if (!d) continue;
        while ((name = g_dir_read_name(d))) {
            gchar *path;
            if (!g_str_has_suffix(name, ".desktop")) continue;
            if (g_hash_table_contains(seen, name)) continue;
            g_hash_table_add(seen, g_strdup(name));
            path = g_build_filename(it->data, name, NULL);
            run_autostart_entry(path);
            g_free(path);
        }
        g_dir_close(d);
    }
    g_hash_table_destroy(seen);
}

void session_autostart(void)
{
    ObtPaths *paths = obt_paths_new();
    GSList *it, *dirs = NULL;
    gchar *user;

    /* the system's autostart, then the user's, like openbox-autostart */
    for (it = obt_paths_config_dirs(paths); it; it = g_slist_next(it))
        if (strcmp(it->data, obt_paths_config_home(paths)))
            dirs = g_slist_prepend(dirs, it->data);
    for (it = dirs; it; it = g_slist_next(it)) {
        gchar *path = g_build_filename(it->data, "owbox", "autostart", NULL);
        run_autostart_script(path);
        g_free(path);
    }
    g_slist_free(dirs);

    user = g_build_filename(obt_paths_config_home(paths), "owbox",
                            "autostart", NULL);
    run_autostart_script(user);
    g_free(user);

    run_xdg_autostart(paths);

    obt_paths_unref(paths);
}

void session_stop_services(void)
{
    /* the session's services stop while the display is still there, and
       later ones don't get this session's display.  A service (a panel)
       that lost its display would fail, and systemd would restart it into
       a session that is gone until it gives up on it. */
    if (in_session) {
        gchar *argv[] = { "/bin/sh", "-c",
            "timeout 5 systemctl --user stop owbox-session.target; "
            "systemctl --user unset-environment WAYLAND_DISPLAY DISPLAY "
            "OWBOX_SOCK", NULL };
        g_spawn_sync(NULL, argv, NULL,
                     G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                     NULL, NULL, NULL, NULL, NULL, NULL);
        in_session = FALSE;
    }
}

void session_shutdown(void)
{
}
