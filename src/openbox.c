/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   openbox.c for owbox (ported from the Openbox window manager)
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

#include "panels.h"
#include "debug.h"
#include "openbox.h"
#include "event.h"
#include "menu.h"
#include "client.h"
#include "screen.h"
#include "actions.h"
#include "focus.h"
#include "focus_cycle.h"
#include "focus_cycle_indicator.h"
#include "focus_cycle_popup.h"
#include "moveresize.h"
#include "frame.h"
#include "framerender.h"
#include "keyboard.h"
#include "mouse.h"
#include "menuframe.h"
#include "grab.h"
#include "config.h"
#include "ping.h"
#include "prompt.h"
#include "server.h"
#include "seat.h"
#include "output.h"
#include "icons.h"
#include "ipc.h"
#include "session.h"
#include "idle.h"
#include "snap.h"
#include "gettext.h"
#include "render/render.h"
#include "render/theme.h"
#include "obt/paths.h"
#include "obt/xml.h"

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib-unix.h>
#include <gio/gio.h>

RrInstance   *ob_rr_inst;
RrImageCache *ob_rr_icons;
RrTheme      *ob_rr_theme;
GMainLoop    *ob_main_loop;
gint          ob_screen = 0;
gboolean      ob_replace_wm = FALSE;
gboolean      ob_debug_xinerama = FALSE;
const gchar  *ob_locale_msg = NULL;

static ObState   state;
static gboolean  reconfigure = FALSE;
static gboolean  restart = FALSE;
static gint      exitcode = 0;
static gchar    *config_file = NULL;
static gchar    *startup_cmd = NULL;
static gboolean  no_autostart = FALSE;
/*! The config file that was loaded, watched for changes */
const gchar *const ob_config_domains[] = { "owbox", "openbox", NULL };

static gchar    *loaded_config_file = NULL;
static GFileMonitor *config_monitor = NULL;
static guint     config_reload_timer = 0;

static void parse_args(gint *argc, gchar **argv);
static void run_startup_cmd(void);
static void watch_config(void);

static gboolean signal_handler(gpointer data)
{
    gint signal = GPOINTER_TO_INT(data);

    switch (signal) {
    case SIGUSR1:
        ob_debug("Caught signal %d. Restarting.", signal);
        ob_restart();
        break;
    case SIGUSR2:
    case SIGHUP:
        ob_debug("Caught signal %d. Reconfiguring.", signal);
        ob_reconfigure();
        break;
    default:
        ob_debug("Caught signal %d. Exiting.", signal);
        /* TERM and INT return a 0 code */
        ob_exit(!(signal == SIGTERM || signal == SIGINT));
    }
    return G_SOURCE_CONTINUE;
}

/*! Load rc.xml.  owbox reads its own config first, and then Openbox's, so
  it is a drop in replacement. */
static void load_config(gchar **xml_error_string)
{
    ObtXmlInst *i;
    gboolean loaded = FALSE;

    /* startup the parsing so everything can register sections
       of the rc */
    i = obt_xml_instance_new();

    /* register all the available actions */
    actions_startup(reconfigure);
    /* start up config which sets up with the parser */
    config_startup(i);

    g_free(loaded_config_file);
    loaded_config_file = NULL;

    /* parse/load user options */
    if (config_file) {
        if (obt_xml_load_file(i, config_file, "openbox_config")) {
            loaded = TRUE;
            loaded_config_file = g_strdup(config_file);
        }
    } else {
        /* owbox's own config, or Openbox's */
        if (obt_xml_load_config_file_domains(i, ob_config_domains, "rc.xml",
                                             "openbox_config"))
        {
            loaded = TRUE;
            loaded_config_file = obt_xml_last_loaded_file(i);
        }
    }

    if (loaded) {
        ob_debug("Loaded config file %s", loaded_config_file);
        /* owbox: the scale comes first, the fonts are opened at it */
        config_read_scale(obt_xml_root(i));
    }
    RrInstanceSetScale(ob_rr_inst, config_scale);
    if (loaded) {
        obt_xml_tree_from_root(i);
        obt_xml_close(i);
    }
    else
        g_message(_("Unable to find a valid config file, using some simple defaults"));

    if (obt_xml_last_error(i)) {
        *xml_error_string = g_strdup_printf(
            _("One or more XML syntax errors were found while parsing the owbox configuration files.  See stdout for more information.  The last error seen was in file \"%s\" line %d, with message: %s"),
            obt_xml_last_error_file(i),
            obt_xml_last_error_line(i),
            obt_xml_last_error_message(i));
    }

    /* we're done with parsing now, kill it */
    obt_xml_instance_unref(i);
}

static void load_theme(void)
{
    RrTheme *theme;
    if ((theme = RrThemeNew(ob_rr_inst, config_theme, TRUE,
                            config_font_activewindow,
                            config_font_inactivewindow,
                            config_font_menutitle,
                            config_font_menuitem,
                            config_font_activeosd,
                            config_font_inactiveosd)))
    {
        RrThemeFree(ob_rr_theme);
        ob_rr_theme = theme;
    }
    if (ob_rr_theme == NULL)
        ob_exit_with_error(_("Unable to load a theme."));
}

gint main(gint argc, gchar **argv)
{
    gchar *program_name;

    ob_set_state(OB_STATE_STARTING);

    ob_debug_startup();

    /* initialize the locale */
    if (!(ob_locale_msg = setlocale(LC_MESSAGES, "")))
        g_message("Couldn't set messages locale category from environment.");
    if (!setlocale(LC_ALL, ""))
        g_message("Couldn't set locale from environment.");

    if (chdir(g_get_home_dir()) == -1)
        g_message(_("Unable to change to home directory \"%s\": %s"),
                  g_get_home_dir(), g_strerror(errno));

    /* parse the command line args, which can change the argv[0] */
    parse_args(&argc, argv);

    program_name = g_path_get_basename(argv[0]);
    g_set_prgname(program_name);

    /* unset this so we don't pass it on unknowingly */
    g_unsetenv("DESKTOP_STARTUP_ID");

    ob_main_loop = g_main_loop_new(NULL, FALSE);

    /* set up signal handlers, they are called from the mainloop
       in the main program's thread */
    g_unix_signal_add(SIGUSR1, signal_handler, GINT_TO_POINTER(SIGUSR1));
    g_unix_signal_add(SIGUSR2, signal_handler, GINT_TO_POINTER(SIGUSR2));
    g_unix_signal_add(SIGTERM, signal_handler, GINT_TO_POINTER(SIGTERM));
    g_unix_signal_add(SIGINT, signal_handler, GINT_TO_POINTER(SIGINT));
    g_unix_signal_add(SIGHUP, signal_handler, GINT_TO_POINTER(SIGHUP));
    signal(SIGPIPE, SIG_IGN);

    ob_rr_inst = RrInstanceNew();
    if (ob_rr_inst == NULL)
        ob_exit_with_error(_("Failed to initialize the obrender library."));
    /* Saving 3 resizes of an RrImage makes a lot of sense for icons, as there
       are generally 3 icon sizes needed: the titlebar icon, the menu icon,
       and the alt-tab icon
    */
    ob_rr_icons = RrImageCacheNew(3);

    /* the config is needed before the server starts, it has the keyboard
       layout and the outputs' settings */
    {
        gchar *xml_error_string = NULL;
        load_config(&xml_error_string);
        if (xml_error_string) {
            g_message("%s", xml_error_string);
            g_free(xml_error_string);
        }
    }

    server_init();
    server_attach_to_main_context(g_main_context_default());
    server_start();
    ipc_startup();
    session_environment();
    idle_startup();

    do {
        gchar *xml_error_string = NULL;
        ObPrompt *xmlprompt = NULL;

        if (reconfigure)
            load_config(&xml_error_string);

        /* load the theme specified in the rc file */
        load_theme();

        if (reconfigure) {
            GList *it;

            seat_reconfigure();
            idle_reconfigure();
            output_reconfigure();

            /* update all existing windows for the new theme */
            for (it = client_list; it; it = g_list_next(it)) {
                ObClient *c = it->data;
                frame_adjust_theme(c->frame);
            }
        }
        event_startup(reconfigure);
        /* focus_backup is used for stacking, so this needs to come before
           anything that calls stacking_add */
        window_startup(reconfigure);
        icons_startup(reconfigure);
        focus_startup(reconfigure);
        focus_cycle_startup(reconfigure);
        focus_cycle_indicator_startup(reconfigure);
        focus_cycle_popup_startup(reconfigure);
        screen_startup(reconfigure);
        grab_startup(reconfigure);
        ping_startup(reconfigure);
        client_startup(reconfigure);
        moveresize_startup(reconfigure);
        snap_startup(reconfigure);
        panels_startup(reconfigure);
        keyboard_startup(reconfigure);
        mouse_startup(reconfigure);
        menu_frame_startup(reconfigure);
        menu_startup(reconfigure);
        prompt_startup(reconfigure);

        if (reconfigure) {
            GList *it;

            /* redecorate all existing windows */
            for (it = client_list; it; it = g_list_next(it)) {
                ObClient *c = it->data;

                /* the new config can change the window's decorations */
                client_setup_decor_and_functions(c, FALSE);
                /* redraw the frames */
                frame_adjust_area(c->frame, TRUE, TRUE, FALSE);
                /* the decor sizes may have changed, so the windows may
                   end up in new positions */
                client_reconfigure(c, FALSE);
            }
        }

        watch_config();

        ob_set_state(OB_STATE_RUNNING);

        if (!reconfigure) {
            if (!no_autostart)
                session_autostart();
            if (startup_cmd) run_startup_cmd();
        }

        reconfigure = FALSE;

        /* look for parsing errors */
        if (xml_error_string) {
            xmlprompt = prompt_show_message(xml_error_string,
                                            _("owbox Syntax Error"),
                                            _("Close"));
            g_free(xml_error_string);
            xml_error_string = NULL;
        }

        g_main_loop_run(ob_main_loop);
        ob_set_state(reconfigure ?
                     OB_STATE_RECONFIGURING : OB_STATE_EXITING);

        if (xmlprompt) {
            prompt_unref(xmlprompt);
            xmlprompt = NULL;
        }

        if (!reconfigure)
            window_unmanage_all();

        prompt_shutdown(reconfigure);
        menu_shutdown(reconfigure);
        menu_frame_shutdown(reconfigure);
        mouse_shutdown(reconfigure);
        keyboard_shutdown(reconfigure);
        panels_shutdown(reconfigure);
        snap_shutdown(reconfigure);
        moveresize_shutdown(reconfigure);
        client_shutdown(reconfigure);
        ping_shutdown(reconfigure);
        grab_shutdown(reconfigure);
        screen_shutdown(reconfigure);
        focus_cycle_popup_shutdown(reconfigure);
        focus_cycle_indicator_shutdown(reconfigure);
        focus_cycle_shutdown(reconfigure);
        focus_shutdown(reconfigure);
        icons_shutdown(reconfigure);
        window_shutdown(reconfigure);
        event_shutdown(reconfigure);
        config_shutdown();
        actions_shutdown(reconfigure);
    } while (reconfigure);

    session_stop_services();
    idle_shutdown();
    ipc_shutdown();
    if (config_monitor)
        g_object_unref(config_monitor);
    server_finish();

    RrThemeFree(ob_rr_theme);
    RrImageCacheUnref(ob_rr_icons);
    RrInstanceFree(ob_rr_inst);

    session_shutdown();

    g_free(program_name);
    g_free(loaded_config_file);

    ob_debug_shutdown();

    return exitcode;
}

/* ObConf (and editors) save rc.xml, reload when that happens, Openbox
   needed "openbox --reconfigure" */
static gboolean config_changed_timeout(gpointer data)
{
    config_reload_timer = 0;
    ob_debug("The config file changed, reconfiguring");
    ob_reconfigure();
    return G_SOURCE_REMOVE;
}

static void config_changed(GFileMonitor *monitor, GFile *file,
                           GFile *other_file, GFileMonitorEvent event,
                           gpointer data)
{
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CREATED &&
        event != G_FILE_MONITOR_EVENT_RENAMED &&
        event != G_FILE_MONITOR_EVENT_MOVED_IN)
        return;
    if (!config_auto_reload) return;
    /* editors write files in several steps */
    if (config_reload_timer) g_source_remove(config_reload_timer);
    config_reload_timer = g_timeout_add(300, config_changed_timeout, NULL);
}

static void watch_config(void)
{
    GFile *f;

    if (config_monitor) {
        g_object_unref(config_monitor);
        config_monitor = NULL;
    }
    if (!loaded_config_file) return;

    f = g_file_new_for_path(loaded_config_file);
    config_monitor = g_file_monitor_file(f, G_FILE_MONITOR_WATCH_MOVES,
                                         NULL, NULL);
    g_object_unref(f);
    if (config_monitor)
        g_signal_connect(config_monitor, "changed",
                         G_CALLBACK(config_changed), NULL);
}

const gchar* ob_config_file(void)
{
    return loaded_config_file;
}

static void print_version(void)
{
    g_print("owbox %s\n", OWBOX_VERSION);
    g_print("Based on the Openbox window manager\n");
    g_print(_("Copyright (c)"));
    g_print(" 2004   Mikael Magnusson\n");
    g_print(_("Copyright (c)"));
    g_print(" 2002   Dana Jansens\n\n");
    g_print("This program comes with ABSOLUTELY NO WARRANTY.\n");
    g_print("This is free software, and you are welcome to redistribute it\n");
    g_print("under certain conditions. See the file COPYING for details.\n\n");
}

static void print_help(void)
{
    g_print(_("Syntax: owbox [options]\n"));
    g_print(_("\nOptions:\n"));
    g_print(_("  --help              Display this help and exit\n"));
    g_print(_("  --version           Display the version and exit\n"));
    g_print(_("  --config-file FILE  Specify the path to the config file to use\n"));
    g_print(_("\nPassing messages to a running owbox instance:\n"));
    g_print(_("  --reconfigure       Reload owbox's configuration\n"));
    g_print(_("  --restart           Restart owbox (same as --reconfigure)\n"));
    g_print(_("  --exit              Exit owbox\n"));
    g_print(_("  --msg COMMAND       Send a command (see owbox(1)): an action's\n"
              "                      XML, windows, desktops\n"));
    g_print(_("  --choose-share      Ask which screen or window to share (for\n"
              "                      xdg-desktop-portal-wlr)\n"));
    g_print(_("\nDebugging options:\n"));
    g_print(_("  --startup CMD       Run CMD after starting\n"));
    g_print(_("  --no-autostart      Don't run the autostart programs\n"));
    g_print(_("  --debug             Display debugging output\n"));
    g_print(_("  --debug-focus       Display debugging output for focus handling\n"));
    g_print(_("  --debug-wlroots     Display debugging output from wlroots\n"));
    g_print(_("\nPlease report bugs at %s\n"), "https://github.com/owbox/owbox");
}

static void remove_args(gint *argc, gchar **argv, gint index, gint num)
{
    gint i;

    for (i = index; i < *argc - num; ++i)
        argv[i] = argv[i+num];
    for (; i < *argc; ++i)
        argv[i] = NULL;
    *argc -= num;
}

static void run_startup_cmd(void)
{
    gchar **argv = NULL;
    GError *e = NULL;
    gboolean ok;

    if (!g_shell_parse_argv(startup_cmd, NULL, &argv, &e)) {
        g_message("Error parsing startup command: %s",
                  e->message);
        g_error_free(e);
        return;
    }
    ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &e);
    if (!ok) {
        g_message("Error launching startup command: %s",
                  e->message);
        g_error_free(e);
    }
    g_strfreev(argv);
}

static void parse_args(gint *argc, gchar **argv)
{
    gint i;
    const gchar *remote = NULL;

    for (i = 1; i < *argc; ++i) {
        if (!strcmp(argv[i], "--version")) {
            print_version();
            exit(0);
        }
        else if (!strcmp(argv[i], "--help")) {
            print_help();
            exit(0);
        }
        else if (!strcmp(argv[i], "--g-fatal-warnings")) {
            g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
        }
        else if (!strcmp(argv[i], "--replace") ||
                 !strcmp(argv[i], "--sm-disable") ||
                 !strcmp(argv[i], "--sync")) {
            /* Openbox options that mean nothing for a Wayland compositor */
        }
        else if (!strcmp(argv[i], "--startup")) {
            if (i == *argc - 1) /* no args left */
                g_printerr(_("%s requires an argument\n"), "--startup");
            else {
                /* this will be in the current locale encoding, which is
                   what we want */
                startup_cmd = argv[i+1];
                remove_args(argc, argv, i, 2);
                --i; /* this arg was removed so go back */
                ob_debug("--startup %s", startup_cmd);
            }
        }
        else if (!strcmp(argv[i], "--debug")) {
            ob_debug_enable(OB_DEBUG_NORMAL, TRUE);
            ob_debug_enable(OB_DEBUG_APP_BUGS, TRUE);
        }
        else if (!strcmp(argv[i], "--debug-focus")) {
            ob_debug_enable(OB_DEBUG_FOCUS, TRUE);
        }
        else if (!strcmp(argv[i], "--debug-session")) {
            ob_debug_enable(OB_DEBUG_SM, TRUE);
        }
        else if (!strcmp(argv[i], "--no-autostart")) {
            no_autostart = TRUE;
        }
        else if (!strcmp(argv[i], "--debug-wlroots")) {
            ob_debug_enable(OB_DEBUG_WLROOTS, TRUE);
        }
        else if (!strcmp(argv[i], "--reconfigure")) {
            remote = "reconfigure";
        }
        else if (!strcmp(argv[i], "--restart")) {
            remote = "restart";
        }
        else if (!strcmp(argv[i], "--exit")) {
            remote = "exit";
        }
        else if (!strcmp(argv[i], "--choose-share")) {
            remote = "choose-share";
        }
        else if (!strcmp(argv[i], "--msg")) {
            if (i == *argc - 1) /* no args left */
                g_printerr(_("%s requires an argument\n"), "--msg");
            else
                remote = argv[++i];
        }
        else if (!strcmp(argv[i], "--config-file")) {
            if (i == *argc - 1) /* no args left */
                g_printerr(_("%s requires an argument\n"), "--config-file");
            else {
                /* this will be in the current locale encoding, which is
                   what we want */
                config_file = argv[i+1];
                ++i; /* skip the argument */
                ob_debug("--config-file %s", config_file);
            }
        }
        else {
            gchar *err = g_strdup_printf
                (_("Invalid command line argument \"%s\"\n"), argv[i]);
            ob_exit_with_error(err);
        }
    }

    if (remote && !strcmp(remote, "choose-share"))
        exit(ipc_choose_share() ? EXIT_SUCCESS : EXIT_FAILURE);
    if (remote)
        /* tell the running owbox, see ipc.c */
        exit(ipc_send_command(remote) ? EXIT_SUCCESS : EXIT_FAILURE);
}

void ob_exit_with_error(const gchar *msg)
{
    g_message("%s", msg);
    exit(EXIT_FAILURE);
}

void ob_restart_other(const gchar *path)
{
    /* a Wayland compositor can't be replaced without its clients going
       away, so this is a restart of owbox itself */
    g_message("Restarting another window manager is not possible with "
              "Wayland, restarting owbox instead");
    ob_restart();
}

void ob_restart(void)
{
    /* Openbox restarts by running itself again.  That would close all the
       clients of a Wayland compositor, so owbox reloads everything instead,
       which is what a restart is used for. */
    restart = TRUE;
    ob_reconfigure();
}

void ob_reconfigure(void)
{
    reconfigure = TRUE;
    ob_exit(0);
}

void ob_exit(gint code)
{
    exitcode = code;
    g_main_loop_quit(ob_main_loop);
}

void ob_exit_replace(void)
{
    ob_exit(0);
}

ObState ob_state(void)
{
    return state;
}

void ob_set_state(ObState s)
{
    state = s;
}
