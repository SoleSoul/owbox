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

#include <wlr/types/wlr_idle_notify_v1.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <string.h>
#include <unistd.h>

#define LOGIND "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER LOGIND ".Manager"
#define LOGIND_SESSION LOGIND ".Session"

#define SCREENSAVER "org.freedesktop.ScreenSaver"

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
static gboolean inhibited;     /* by a window or an application */
static gboolean window_inhibited; /* the idle-inhibit protocol */
static gboolean logind_inhibited; /* systemd-inhibit --what=idle */
static gboolean active;        /* org.freedesktop.ScreenSaver's Active */
static gint64 lock_started;    /* when a locker was last started */

/* logind, in a session */
static GDBusConnection *system_bus;
static GDBusConnection *session_bus;
static guint screensaver_name, screensaver_objs[2], owner_signal;
static guint32 next_cookie = 1;
static GCancellable *cancel;
static gchar *session_path;
static guint sleep_signal, session_signal, inhibited_signal;
static gint sleep_fd = -1;     /* the delay inhibitor for suspending */
static guint sleep_timer;

static void take_sleep_inhibitor(void);
static void set_idle_hint(gboolean idle);
static void update_active(void);

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
    set_idle_hint(TRUE);
    update_active();
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
        set_idle_hint(FALSE);
        update_active();
        restart_timer();
    }
    else if (!timer)
        restart_timer();
}

/*! An application keeping the screen on through D-Bus */
typedef struct {
    guint32 cookie;
    gchar *sender; /* its unique bus name */
    gchar *app;
} ObAppInhibitor;

static GList *app_inhibitors; /* ObAppInhibitor* */

static void update_inhibited(void)
{
    gboolean inh = window_inhibited || app_inhibitors || logind_inhibited;

    if (inh == inhibited) return;
    inhibited = inh;
    /* idle clients (swayidle) wait too */
    if (server.idle_notifier)
        wlr_idle_notifier_v1_set_inhibited(server.idle_notifier, inh);
    /* the time counts from when the video stopped */
    last_activity = g_get_monotonic_time();
    restart_timer();
}

void idle_set_inhibited(gboolean inh)
{
    window_inhibited = inh;
    update_inhibited();
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

static void set_locked_hint(gboolean locked);

void idle_set_locked(gboolean locked)
{
    if (locked) {
        lock_started = 0;
        /* locked, suspending can go on */
        release_sleep_inhibitor();
    }
    set_locked_hint(locked);
    update_active();
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
    /* not what an owbox that crashed left */
    set_idle_hint(screens_off);
    set_locked_hint(seat_is_locked());
    session_signal = g_dbus_connection_signal_subscribe(
        system_bus, LOGIND, LOGIND_SESSION, "Lock", session_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_session_signal, NULL, NULL);
}

/* logind's IdleHint (for its IdleAction) and LockedHint (loginctl shows
   it), which a desktop sets for its session */
static void set_session_hint(const gchar *method, gboolean value)
{
    if (!system_bus || !session_path) return;
    g_dbus_connection_call(system_bus, LOGIND, session_path, LOGIND_SESSION,
                           method, g_variant_new("(b)", value), NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, cancel, NULL, NULL);
}

static void set_idle_hint(gboolean idle)
{
    set_session_hint("SetIdleHint", idle);
}

static void set_locked_hint(gboolean locked)
{
    set_session_hint("SetLockedHint", locked);
}

/* logind's idle inhibitors (systemd-inhibit --what=idle --mode=block),
   from its BlockInhibited, "what"s separated by colons */
static void set_block_inhibited(const gchar *what)
{
    gchar **list = g_strsplit(what, ":", -1);
    gboolean inh = g_strv_contains((const gchar *const*)list, "idle");

    g_strfreev(list);
    if (inh == logind_inhibited) return;
    ob_debug(inh ? "logind has an idle inhibitor, keeping the screen on" :
             "logind's idle inhibitors are gone");
    logind_inhibited = inh;
    update_inhibited();
}

static void got_block_inhibited(GObject *src, GAsyncResult *res,
                                gpointer data)
{
    GVariant *r, *v;
    GError *e = NULL;

    r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (!r) {
        if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            ob_debug("No inhibitors from logind: %s", e->message);
        g_error_free(e);
        return;
    }
    g_variant_get(r, "(v)", &v);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING))
        set_block_inhibited(g_variant_get_string(v, NULL));
    g_variant_unref(v);
    g_variant_unref(r);
}

static void get_block_inhibited(void)
{
    g_dbus_connection_call(system_bus, LOGIND, LOGIND_PATH,
                           "org.freedesktop.DBus.Properties", "Get",
                           g_variant_new("(ss)", LOGIND_MANAGER,
                                         "BlockInhibited"),
                           G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1,
                           cancel, got_block_inhibited, NULL);
}

static void on_manager_changed(GDBusConnection *bus, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *signal, GVariant *params,
                               gpointer data)
{
    const gchar *what;
    GVariant *changed;
    const gchar **invalidated;

    g_variant_get(params, "(&s@a{sv}^a&s)", NULL, &changed, &invalidated);
    if (g_variant_lookup(changed, "BlockInhibited", "&s", &what))
        set_block_inhibited(what);
    else if (g_strv_contains(invalidated, "BlockInhibited"))
        get_block_inhibited();
    g_variant_unref(changed);
    g_free(invalidated);
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
    inhibited_signal = g_dbus_connection_signal_subscribe(
        system_bus, LOGIND, "org.freedesktop.DBus.Properties",
        "PropertiesChanged", LOGIND_PATH, LOGIND_MANAGER,
        G_DBUS_SIGNAL_FLAGS_NONE, on_manager_changed, NULL, NULL);
    get_block_inhibited();
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
    if (inhibited_signal)
        g_dbus_connection_signal_unsubscribe(system_bus, inhibited_signal);
    sleep_signal = session_signal = inhibited_signal = 0;
    logind_inhibited = FALSE;
    release_sleep_inhibitor();
    g_free(session_path);
    session_path = NULL;
    g_clear_object(&system_bus);
}

/* ---- org.freedesktop.ScreenSaver ----

   How applications ask to keep the screen on (VLC, Steam, Chromium), and
   what xdg-desktop-portal-gtk's Inhibit uses outside GNOME. */

static const gchar screensaver_xml[] =
    "<node>"
    "  <interface name='" SCREENSAVER "'>"
    "    <method name='Inhibit'>"
    "      <arg type='s' name='application_name' direction='in'/>"
    "      <arg type='s' name='reason_for_inhibit' direction='in'/>"
    "      <arg type='u' name='cookie' direction='out'/>"
    "    </method>"
    "    <method name='UnInhibit'>"
    "      <arg type='u' name='cookie' direction='in'/>"
    "    </method>"
    "    <method name='SimulateUserActivity'/>"
    "    <method name='GetActive'>"
    "      <arg type='b' direction='out'/>"
    "    </method>"
    "    <method name='Lock'/>"
    "    <signal name='ActiveChanged'>"
    "      <arg type='b'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static const gchar *const screensaver_paths[] = {
    "/org/freedesktop/ScreenSaver",
    "/ScreenSaver" /* older applications */
};

static void app_inhibitor_free(ObAppInhibitor *a)
{
    g_free(a->sender);
    g_free(a->app);
    g_free(a);
}

static void app_uninhibit(GList *it)
{
    ObAppInhibitor *a = it->data;

    ob_debug("%s lets the screen turn off", a->app);
    app_inhibitor_free(a);
    app_inhibitors = g_list_delete_link(app_inhibitors, it);
    update_inhibited();
}

/* the screen saver is active when the monitors are off or it's locked */
static void update_active(void)
{
    gboolean a = screens_off || seat_is_locked();
    guint i;

    if (a == active) return;
    active = a;
    if (!screensaver_name) return;
    for (i = 0; i < G_N_ELEMENTS(screensaver_paths); ++i)
        g_dbus_connection_emit_signal(session_bus, NULL,
                                      screensaver_paths[i], SCREENSAVER,
                                      "ActiveChanged",
                                      g_variant_new("(b)", active), NULL);
}

static void screensaver_call(GDBusConnection *bus, const gchar *sender,
                             const gchar *path, const gchar *iface,
                             const gchar *method, GVariant *params,
                             GDBusMethodInvocation *inv, gpointer data)
{
    if (!strcmp(method, "Inhibit")) {
        ObAppInhibitor *a = g_new0(ObAppInhibitor, 1);
        const gchar *app, *reason;

        g_variant_get(params, "(&s&s)", &app, &reason);
        a->cookie = next_cookie++;
        a->sender = g_strdup(sender);
        a->app = g_strdup(*app ? app : sender);
        ob_debug("%s keeps the screen on: %s", a->app, reason);
        app_inhibitors = g_list_prepend(app_inhibitors, a);
        update_inhibited();
        g_dbus_method_invocation_return_value(inv,
                                              g_variant_new("(u)", a->cookie));
    }
    else if (!strcmp(method, "UnInhibit")) {
        guint32 cookie;
        GList *it;

        g_variant_get(params, "(u)", &cookie);
        for (it = app_inhibitors; it; it = g_list_next(it)) {
            ObAppInhibitor *a = it->data;
            if (a->cookie == cookie && !strcmp(a->sender, sender)) {
                app_uninhibit(it);
                break;
            }
        }
        g_dbus_method_invocation_return_value(inv, NULL);
    }
    else if (!strcmp(method, "SimulateUserActivity")) {
        idle_activity();
        if (server.idle_notifier)
            wlr_idle_notifier_v1_notify_activity(server.idle_notifier,
                                                 server.seat);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
    else if (!strcmp(method, "GetActive"))
        g_dbus_method_invocation_return_value(inv,
                                              g_variant_new("(b)", active));
    else if (!strcmp(method, "Lock")) {
        idle_lock();
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

/* an application that quits (or crashes) without UnInhibit */
static void on_name_owner_changed(GDBusConnection *bus, const gchar *sender,
                                  const gchar *path, const gchar *iface,
                                  const gchar *signal, GVariant *params,
                                  gpointer data)
{
    const gchar *name, *old_owner, *new_owner;
    GList *it, *next;

    g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);
    if (*new_owner || name[0] != ':') return;
    for (it = app_inhibitors; it; it = next) {
        ObAppInhibitor *a = it->data;
        next = g_list_next(it);
        if (!strcmp(a->sender, name))
            app_uninhibit(it);
    }
}

static void on_screensaver_lost(GDBusConnection *bus, const gchar *name,
                                gpointer data)
{
    /* another program has it (a screen saver of another desktop) */
    ob_debug("Unable to serve %s on the session bus", name);
}

static void screensaver_startup(void)
{
    static const GDBusInterfaceVTable vtable = { screensaver_call };
    GDBusNodeInfo *info;
    GError *e = NULL;
    guint i;

    if (!(session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &e))) {
        ob_debug("No session bus: %s", e->message);
        g_error_free(e);
        return;
    }
    info = g_dbus_node_info_new_for_xml(screensaver_xml, NULL);
    for (i = 0; i < G_N_ELEMENTS(screensaver_paths); ++i)
        screensaver_objs[i] = g_dbus_connection_register_object(
            session_bus, screensaver_paths[i], info->interfaces[0], &vtable,
            NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
    owner_signal = g_dbus_connection_signal_subscribe(
        session_bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_name_owner_changed, NULL, NULL);
    screensaver_name = g_bus_own_name_on_connection(
        session_bus, SCREENSAVER, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
        on_screensaver_lost, NULL, NULL);
}

static void screensaver_shutdown(void)
{
    guint i;

    if (!session_bus) return;
    if (screensaver_name) g_bus_unown_name(screensaver_name);
    for (i = 0; i < G_N_ELEMENTS(screensaver_objs); ++i)
        if (screensaver_objs[i])
            g_dbus_connection_unregister_object(session_bus,
                                                screensaver_objs[i]);
    if (owner_signal)
        g_dbus_connection_signal_unsubscribe(session_bus, owner_signal);
    screensaver_name = owner_signal = 0;
    memset(screensaver_objs, 0, sizeof(screensaver_objs));
    g_list_free_full(app_inhibitors, (GDestroyNotify)app_inhibitor_free);
    app_inhibitors = NULL;
    g_clear_object(&session_bus);
}

/* ---- startup ---- */

void idle_startup(void)
{
    /* the monitors of a nested owbox are windows of another session */
    enabled = !server_is_nested();
    last_activity = g_get_monotonic_time();
    restart_timer();
    /* locking, suspending and keeping awake are the session's, not a
       test's, unless the test has its own buses and asks */
    if (server_is_session() || g_getenv("OWBOX_TEST_BUSES")) {
        logind_startup();
        screensaver_startup();
    }
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
    screensaver_shutdown();
    logind_shutdown();
}
