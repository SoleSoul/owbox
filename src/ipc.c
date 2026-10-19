/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ipc.c for owbox
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

/* A Unix socket for controlling owbox, which replaces the X root window
   messages that "openbox --reconfigure" sends, and the X tools (wmctrl,
   xdotool) people script Openbox with.

   The protocol is one command per line, and the reply ends with an empty
   line.  The reply's first line is "ok" or "error <message>".
     reconfigure | restart | exit
     action <action xml>   eg: action <action name="GoToDesktop"><to>2</to></action>
     windows               one JSON object per window, in stacking order
     desktops              a JSON object with the desktops
     config-file           the path of the rc.xml in use
     outputs               one JSON object per monitor
     choose-share          show the menu of what to share, the reply is
                           "Monitor: <output>" or "Window: <identifier>", or
                           nothing when the user didn't choose */

#include "ipc.h"
#include "openbox.h"
#include "actions.h"
#include "client.h"
#include "frame.h"
#include "focus.h"
#include "screen.h"
#include "stacking.h"
#include "toplevel.h"
#include "share_menu.h"
#include "output.h"
#include "server.h"

#include <wlr/types/wlr_output.h>
#include "debug.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <gio/gio.h>
#include <libxml/parser.h>

static GSocketService *service = NULL;
static gchar *socket_path = NULL;

gchar* ipc_socket_path(void)
{
    const gchar *runtime = g_getenv("XDG_RUNTIME_DIR");
    const gchar *display = g_getenv("WAYLAND_DISPLAY");
    if (!runtime || !display) return NULL;
    return g_strdup_printf("%s/owbox-%s.sock", runtime, display);
}

static void json_string(GString *out, const gchar *s)
{
    const gchar *p;
    g_string_append_c(out, '"');
    for (p = s ? s : ""; *p; ++p) {
        switch (*p) {
        case '"': g_string_append(out, "\\\""); break;
        case '\\': g_string_append(out, "\\\\"); break;
        case '\n': g_string_append(out, "\\n"); break;
        case '\t': g_string_append(out, "\\t"); break;
        default:
            if ((guchar)*p < 0x20)
                g_string_append_printf(out, "\\u%04x", *p);
            else
                g_string_append_c(out, *p);
        }
    }
    g_string_append_c(out, '"');
}

static void list_windows(GString *out)
{
    GList *it;
    for (it = stacking_list; it; it = g_list_next(it)) {
        ObClient *c;
        if (!WINDOW_IS_CLIENT(it->data)) continue;
        c = it->data;
        g_string_append(out, "{\"id\":");
        g_string_append_printf(out, "%lu", (gulong)(gsize)c);
        g_string_append(out, ",\"title\":");
        json_string(out, c->title);
        g_string_append(out, ",\"app_id\":");
        json_string(out, c->class);
        g_string_append(out, ",\"identifier\":");
        json_string(out, toplevel_identifier(c));
        g_string_append_printf(out,
            ",\"desktop\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d",
            c->desktop == DESKTOP_ALL ? -1 : (gint)c->desktop,
            c->frame->area.x, c->frame->area.y,
            c->area.width, c->area.height);
        /* the frame's size around the client, left, top, right, bottom */
        g_string_append_printf(out, ",\"frame\":[%d,%d,%d,%d]",
            c->frame->size.left, c->frame->size.top,
            c->frame->size.right, c->frame->size.bottom);
        g_string_append_printf(out,
            ",\"focused\":%s,\"iconic\":%s,\"maximized\":%s,"
            "\"fullscreen\":%s,\"shaded\":%s}\n",
            client_focused(c) ? "true" : "false",
            c->iconic ? "true" : "false",
            c->max_horz && c->max_vert ? "true" : "false",
            c->fullscreen ? "true" : "false",
            c->shaded ? "true" : "false");
    }
}

static void list_desktops(GString *out)
{
    guint i;
    g_string_append_printf(out, "{\"current\":%u,\"names\":[", screen_desktop);
    for (i = 0; i < screen_num_desktops; ++i) {
        if (i) g_string_append_c(out, ',');
        json_string(out, screen_desktop_names ? screen_desktop_names[i] : "");
    }
    g_string_append(out, "]}\n");
}

static void list_outputs(GString *out)
{
    static const gchar *transforms[] = {
        "normal", "90", "180", "270", "flipped", "flipped-90",
        "flipped-180", "flipped-270"
    };
    ObOutput *o;

    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_output *w = o->wlr_output;
        struct wlr_output_mode *m;
        gboolean first = TRUE;

        g_string_append(out, "{\"name\":");
        json_string(out, w->name);
        g_string_append(out, ",\"make\":");
        json_string(out, w->make);
        g_string_append(out, ",\"model\":");
        json_string(out, w->model);
        g_string_append_printf(out,
            ",\"enabled\":%s,\"power\":%s,\"x\":%d,\"y\":%d,\"width\":%d,"
            "\"height\":%d,\"refresh\":%d,\"scale\":%g,\"transform\":",
            w->enabled || o->powered_off ? "true" : "false",
            o->powered_off ? "false" : "true", o->area.x, o->area.y,
            w->width, w->height, w->refresh, w->scale);
        json_string(out, transforms[w->transform & 7]);
        g_string_append(out, ",\"modes\":[");
        wl_list_for_each(m, &w->modes, link) {
            g_string_append_printf(out, "%s{\"width\":%d,\"height\":%d,"
                                   "\"refresh\":%d,\"preferred\":%s}",
                                   first ? "" : ",", m->width, m->height,
                                   m->refresh,
                                   m->preferred ? "true" : "false");
            first = FALSE;
        }
        g_string_append(out, "]}\n");
    }
}

static gboolean run_action_xml(const gchar *xml, gchar **err)
{
    xmlDocPtr doc;
    xmlNodePtr root, n;
    GSList *acts = NULL;

    doc = xmlReadMemory(xml, strlen(xml), "ipc.xml", NULL,
                        XML_PARSE_NOBLANKS | XML_PARSE_NONET |
                        XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) {
        *err = g_strdup("invalid XML");
        return FALSE;
    }
    root = xmlDocGetRootElement(doc);
    /* one <action> or several inside any root element */
    if (root && !xmlStrcmp(root->name, (const xmlChar*)"action")) {
        ObActionsAct *a = actions_parse(root);
        if (a) acts = g_slist_append(acts, a);
    } else if (root) {
        for (n = root->children; n; n = n->next)
            if (!xmlStrcmp(n->name, (const xmlChar*)"action")) {
                ObActionsAct *a = actions_parse(n);
                if (a) acts = g_slist_append(acts, a);
            }
    }
    xmlFreeDoc(doc);

    if (!acts) {
        *err = g_strdup("no valid actions");
        return FALSE;
    }
    actions_run_acts(acts, OB_USER_ACTION_NONE, 0, -1, -1, 0,
                     OB_FRAME_CONTEXT_NONE, focus_client);
    g_slist_free_full(acts, (GDestroyNotify)actions_act_unref);
    return TRUE;
}

static gchar* handle_command(const gchar *line)
{
    GString *out = g_string_new(NULL);
    gchar *err = NULL;

    if (!strcmp(line, "reconfigure"))
        ob_reconfigure();
    else if (!strcmp(line, "restart"))
        ob_restart();
    else if (!strcmp(line, "exit"))
        ob_exit(0);
    else if (!strcmp(line, "windows"))
        list_windows(out);
    else if (!strcmp(line, "desktops"))
        list_desktops(out);
    else if (!strcmp(line, "outputs"))
        list_outputs(out);
    else if (!strcmp(line, "config-file")) {
        if (ob_config_file())
            g_string_append_printf(out, "%s\n", ob_config_file());
    }
    else if (!strncmp(line, "action ", 7)) {
        if (ob_state() != OB_STATE_RUNNING)
            err = g_strdup("not running");
        else
            run_action_xml(line + 7, &err);
    } else
        err = g_strdup_printf("unknown command: %s", line);

    if (err) {
        gchar *r = g_strdup_printf("error %s\n\n", err);
        g_free(err);
        g_string_free(out, TRUE);
        return r;
    }
    g_string_prepend(out, "ok\n");
    g_string_append_c(out, '\n');
    return g_string_free(out, FALSE);
}

static void client_read(GObject *src, GAsyncResult *res, gpointer data);

static void reply_and_continue(GDataInputStream *in, GSocketConnection *conn,
                               const gchar *reply)
{
    GOutputStream *os = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    g_output_stream_write_all(os, reply, strlen(reply), NULL, NULL, NULL);
    g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL,
                                        client_read, conn);
}

static void share_chosen(const gchar *choice, gpointer data)
{
    GDataInputStream *in = data;
    GSocketConnection *conn = g_object_get_data(G_OBJECT(in), "conn");
    gchar *reply = g_strdup_printf("ok\n%s%s\n", choice ? choice : "",
                                   choice ? "\n" : "");
    reply_and_continue(in, conn, reply);
    g_free(reply);
}

static void client_read(GObject *src, GAsyncResult *res, gpointer data)
{
    GDataInputStream *in = G_DATA_INPUT_STREAM(src);
    GSocketConnection *conn = data;
    gchar *line;
    gsize len;

    line = g_data_input_stream_read_line_finish_utf8(in, res, &len, NULL);
    if (!line) {
        g_object_unref(in);
        g_object_unref(conn);
        return;
    }
    if (!strcmp(g_strstrip(line), "choose-share")) {
        /* answered when the user has chosen */
        g_object_set_data(G_OBJECT(in), "conn", conn);
        share_menu_choose(share_chosen, in);
    } else {
        gchar *reply = handle_command(line);
        reply_and_continue(in, conn, reply);
        g_free(reply);
    }
    g_free(line);
}

static gboolean incoming(GSocketService *s, GSocketConnection *conn,
                         GObject *source, gpointer data)
{
    GDataInputStream *in = g_data_input_stream_new(
        g_io_stream_get_input_stream(G_IO_STREAM(conn)));

    g_object_ref(conn);
    g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL,
                                        client_read, conn);
    return TRUE;
}

void ipc_startup(void)
{
    GSocketAddress *addr;
    GError *e = NULL;

    if (!(socket_path = ipc_socket_path())) return;
    unlink(socket_path);

    service = g_socket_service_new();
    addr = g_unix_socket_address_new(socket_path);
    if (!g_socket_listener_add_address(G_SOCKET_LISTENER(service), addr,
                                       G_SOCKET_TYPE_STREAM,
                                       G_SOCKET_PROTOCOL_DEFAULT,
                                       NULL, NULL, &e))
    {
        g_message("Unable to listen on %s: %s", socket_path, e->message);
        g_error_free(e);
        g_object_unref(service);
        service = NULL;
    } else {
        g_signal_connect(service, "incoming", G_CALLBACK(incoming), NULL);
        g_socket_service_start(service);
        g_setenv("OWBOX_SOCK", socket_path, TRUE);
    }
    g_object_unref(addr);
}

void ipc_shutdown(void)
{
    if (service) {
        g_socket_service_stop(service);
        g_socket_listener_close(G_SOCKET_LISTENER(service));
        g_object_unref(service);
        service = NULL;
    }
    if (socket_path) {
        unlink(socket_path);
        g_free(socket_path);
        socket_path = NULL;
    }
}

gboolean ipc_send_command(const gchar *command)
{
    const gchar *path_env = g_getenv("OWBOX_SOCK");
    gchar *path = path_env ? g_strdup(path_env) : ipc_socket_path();
    GSocketClient *client;
    GSocketConnection *conn;
    GSocketAddress *addr;
    GError *e = NULL;
    gboolean ok = FALSE;

    if (!path) {
        g_printerr("owbox is not running (WAYLAND_DISPLAY is not set)\n");
        return FALSE;
    }
    client = g_socket_client_new();
    addr = g_unix_socket_address_new(path);
    conn = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(addr),
                                   NULL, &e);
    if (!conn) {
        g_printerr("Unable to connect to owbox at %s: %s\n", path,
                   e->message);
        g_error_free(e);
    } else {
        GOutputStream *os = g_io_stream_get_output_stream(G_IO_STREAM(conn));
        GDataInputStream *in = g_data_input_stream_new(
            g_io_stream_get_input_stream(G_IO_STREAM(conn)));
        gchar *msg = g_strconcat(command, "\n", NULL);
        gchar *line;
        gboolean first = TRUE;

        g_output_stream_write_all(os, msg, strlen(msg), NULL, NULL, NULL);
        g_free(msg);
        while ((line = g_data_input_stream_read_line_utf8(in, NULL, NULL,
                                                          NULL)))
        {
            if (!*line) {
                g_free(line);
                break;
            }
            if (first) {
                ok = !strcmp(line, "ok");
                if (!ok) g_printerr("%s\n", line);
                first = FALSE;
            } else
                g_print("%s\n", line);
            g_free(line);
        }
        g_object_unref(in);
        g_object_unref(conn);
    }
    g_object_unref(addr);
    g_object_unref(client);
    g_free(path);
    return ok;
}
