/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   idle.c for owbox
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

#include "idle.h"
#include "config.h"
#include "debug.h"
#include "output.h"
#include "seat.h"
#include "server.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <string.h>
#include <unistd.h>

#define LOGIND "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER LOGIND ".Manager"
#define LOGIND_SESSION LOGIND ".Session"

/* how long to hold off suspending for the locker to lock */
#define SLEEP_LOCK_TIMEOUT 3000 /* ms */
/* a locker that was started is given this long to lock before another one
   is started */
#define LOCK_START_TIME (5 * G_USEC_PER_SEC)

/* screen lockers that lock before returning (or forking), so that the
   screen is locked when suspending */
static const gchar *const lockers[][2] = {
    { "swaylock", "swaylock -f" },
    { "gtklock",  "gtklock -d" },
    { "waylock",  "waylock -fork-on-lock" },
    { "hyprlock", "hyprlock" },
};

static gboolean enabled;       /* turning the screens off, not nested */
static guint timer;
static gint64 last_activity;   /* g_get_monotonic_time() */
static gboolean screens_off;
static gboolean inhibited;
static gint64 lock_started;    /* when a locker was last started */

/* logind, in a session */
static GDBusConnection *system_bus;
static GCancellable *cancel;
static gchar *session_path;
static guint sleep_signal, session_signal;
static gint sleep_fd = -1;     /* the delay inhibitor for suspending */
static guint sleep_timer;

static void take_sleep_inhibitor(void);

static gint64 timeout_usec(void)
{
    return (gint64)(config_idle_screen_off * 60 * G_USEC_PER_SEC);
}

static gboolean timeout_func(gpointer data)
{
    gint64 left;

    timer = 0;
    if (!enabled || config_idle_screen_off <= 0 || screens_off || inhibited)
        return G_SOURCE_REMOVE;
    left = last_activity + timeout_usec() - g_get_monotonic_time();
    if (left > 0) {
        /* there was input since the timer started */
        timer = g_timeout_add(MAX(left / 1000, 1), timeout_func, NULL);
        return G_SOURCE_REMOVE;
    }

    ob_debug("Idle, turning the monitors off");
    if (config_idle_lock)
        idle_lock();
    screens_off = TRUE;
    output_set_power_all(FALSE);
    return G_SOURCE_REMOVE;
}

static void restart_timer(void)
{
    if (timer) g_source_remove(timer);
    timer = 0;
    if (enabled && config_idle_screen_off > 0 && !inhibited && !screens_off)
        timer = g_timeout_add(MAX(timeout_usec() / 1000, 1), timeout_func,
                              NULL);
}

void idle_activity(void)
{
    last_activity = g_get_monotonic_time();
    if (screens_off) {
        ob_debug("Input, turning the monitors on");
        screens_off = FALSE;
        output_set_power_all(TRUE);
        restart_timer();
    }
    else if (!timer)
        restart_timer();
}

void idle_set_inhibited(gboolean inh)
{
    if (inh == inhibited) return;
    inhibited = inh;
    /* the time counts from when the video stopped */
    last_activity = g_get_monotonic_time();
    restart_timer();
}

/* ---- locking ---- */

static gchar* lock_command(void)
{
    guint i;

    if (config_idle_lock_command)
        return g_strdup(config_idle_lock_command);
    for (i = 0; i < G_N_ELEMENTS(lockers); ++i) {
        gchar *p = g_find_program_in_path(lockers[i][0]);
        if (p) {
            g_free(p);
            return g_strdup(lockers[i][1]);
        }
    }
    return NULL;
}

void idle_lock(void)
{
    gchar *cmd, **argv = NULL;
    GError *e = NULL;

    if (seat_is_locked()) return;
    if (lock_started &&
        g_get_monotonic_time() - lock_started < LOCK_START_TIME)
        return;

    if (!(cmd = lock_command())) {
        /* not a message, those become dialogs; Lock Screen in the menu
           says it */
        ob_debug("No screen locker to lock the screen with");
        return;
    }
    ob_debug("Locking the screen with %s", cmd);
    if (g_shell_parse_argv(cmd, NULL, &argv, &e) &&
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                      NULL, &e))
        lock_started = g_get_monotonic_time();
    else {
        g_message("Unable to run the screen locker %s: %s", cmd, e->message);
        g_error_free(e);
    }
    g_strfreev(argv);
    g_free(cmd);
}

static void release_sleep_inhibitor(void)
{
    if (sleep_timer) g_source_remove(sleep_timer);
    sleep_timer = 0;
    if (sleep_fd >= 0) {
        close(sleep_fd);
        sleep_fd = -1;
    }
}

void idle_locked(void)
{
    lock_started = 0;
    /* locked, suspending can go on */
    release_sleep_inhibitor();
}

static gboolean sleep_timeout_func(gpointer data)
{
    sleep_timer = 0;
    release_sleep_inhibitor();
    return G_SOURCE_REMOVE;
}

/* ---- logind ---- */

static void on_prepare_for_sleep(GDBusConnection *bus, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *signal, GVariant *params,
                                 gpointer data)
{
    gboolean start;

    g_variant_get(params, "(b)", &start);
    if (start) {
        ob_debug("Suspending");
        if (config_idle_lock && !seat_is_locked())
            idle_lock();
        if (lock_started && !seat_is_locked()) {
            /* hold off suspending until it locks, or a while */
            if (sleep_timer) g_source_remove(sleep_timer);
            sleep_timer = g_timeout_add(SLEEP_LOCK_TIMEOUT,
                                        sleep_timeout_func, NULL);
        }
        else
            release_sleep_inhibitor();
    } else {
        ob_debug("Resumed");
        take_sleep_inhibitor();
        idle_activity();
    }
}

static void on_session_signal(GDBusConnection *bus, const gchar *sender,
                              const gchar *path, const gchar *iface,
                              const gchar *signal, GVariant *params,
                              gpointer data)
{
    gchar *cmd;

    /* loginctl lock-session, the menu's Lock Screen */
    if (strcmp(signal, "Lock")) return;
    if ((cmd = lock_command())) {
        g_free(cmd);
        idle_lock();
    } else {
        /* asked for, so say why nothing happens */
        gchar *argv[] = { "notify-send", "-a", "owbox",
                          "-i", "system-lock-screen",
                          "No screen locker",
                          "Install swaylock to lock the screen.", NULL };
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                      NULL, NULL);
    }
}

static void got_sleep_inhibitor(GObject *src, GAsyncResult *res,
                                gpointer data)
{
    GUnixFDList *fds = NULL;
    GError *e = NULL;
    GVariant *r;

    r = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(src),
                                                        &fds, res, &e);
    if (!r) {
        if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            ob_debug("No sleep inhibitor from logind: %s", e->message);
        g_error_free(e);
        return;
    }
    if (fds && g_unix_fd_list_get_length(fds) > 0) {
        release_sleep_inhibitor();
        sleep_fd = g_unix_fd_list_get(fds, 0, NULL);
    }
    if (fds) g_object_unref(fds);
    g_variant_unref(r);
}

static void take_sleep_inhibitor(void)
{
    if (!system_bus || !config_idle_lock || sleep_fd >= 0) return;
    g_dbus_connection_call_with_unix_fd_list(
        system_bus, LOGIND, LOGIND_PATH, LOGIND_MANAGER, "Inhibit",
        g_variant_new("(ssss)", "sleep", "owbox",
                      "Locking the screen before suspending", "delay"),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, cancel,
        got_sleep_inhibitor, NULL);
}

static void got_session(GObject *src, GAsyncResult *res, gpointer data)
{
    GError *e = NULL;
    GVariant *r;

    r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (!r) {
        if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            ob_debug("Not in a logind session: %s", e->message);
        g_error_free(e);
        return;
    }
    g_variant_get(r, "(o)", &session_path);
    g_variant_unref(r);
    session_signal = g_dbus_connection_signal_subscribe(
        system_bus, LOGIND, LOGIND_SESSION, "Lock", session_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_session_signal, NULL, NULL);
}

static void logind_startup(void)
{
    GError *e = NULL;

    if (!(system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &e))) {
        ob_debug("No system bus: %s", e->message);
        g_error_free(e);
        return;
    }
    cancel = g_cancellable_new();
    sleep_signal = g_dbus_connection_signal_subscribe(
        system_bus, LOGIND, LOGIND_MANAGER, "PrepareForSleep", LOGIND_PATH,
        NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_prepare_for_sleep, NULL, NULL);
    /* the session owbox runs in */
    g_dbus_connection_call(system_bus, LOGIND, LOGIND_PATH, LOGIND_MANAGER,
                           "GetSession", g_variant_new("(s)", "auto"),
                           G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1,
                           cancel, got_session, NULL);
    take_sleep_inhibitor();
}

static void logind_shutdown(void)
{
    if (!system_bus) return;
    g_cancellable_cancel(cancel);
    g_clear_object(&cancel);
    if (sleep_signal)
        g_dbus_connection_signal_unsubscribe(system_bus, sleep_signal);
    if (session_signal)
        g_dbus_connection_signal_unsubscribe(system_bus, session_signal);
    sleep_signal = session_signal = 0;
    release_sleep_inhibitor();
    g_free(session_path);
    session_path = NULL;
    g_clear_object(&system_bus);
}

/* ---- startup ---- */

void idle_startup(void)
{
    /* the monitors of a nested owbox are windows of another session */
    enabled = !server_is_nested();
    last_activity = g_get_monotonic_time();
    restart_timer();
    /* locking and suspending are the session's, not a test's */
    if (server_is_session())
        logind_startup();
}

void idle_reconfigure(void)
{
    last_activity = g_get_monotonic_time();
    restart_timer();
    if (system_bus) {
        if (config_idle_lock)
            take_sleep_inhibitor();
        else
            release_sleep_inhibitor();
    }
}

void idle_shutdown(void)
{
    if (timer) g_source_remove(timer);
    timer = 0;
    logind_shutdown();
}
