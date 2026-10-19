/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   ping.c for the Openbox window manager
   Copyright (c) 2008        Dana Jansens
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

/* Openbox pings a window it asked to close (_NET_WM_PING), and offers to
   kill it when it doesn't answer.  The shell protocols do the pinging
   (xdg_wm_base.ping, or _NET_WM_PING through Xwayland), and they don't
   report pongs, so instead of waiting for a pong this looks at each
   interval whether the last ping is still unanswered. */

#include "ping.h"
#include "client.h"
#include "debug.h"

typedef struct _ObPingTarget
{
    ObClient *client;
    ObPingEventHandler h;
    guint loopid;
    gint waiting;
} ObPingTarget;

static GSList *ping_targets = NULL;

#define PING_TIMEOUT 3000 /* in MS */
/*! Warn the user after this many PING_TIMEOUT intervals */
#define PING_TIMEOUT_WARN 2

static void     ping_end(ObClient *client, gpointer data);
static gboolean ping_timeout(gpointer data);

void ping_startup(gboolean reconfigure)
{
    if (reconfigure) return;

    /* listen for clients to disappear */
    client_add_destroy_notify(ping_end, NULL);
}

void ping_shutdown(gboolean reconfigure)
{
    if (reconfigure) return;

    while (ping_targets)
        ping_end(((ObPingTarget*)ping_targets->data)->client, NULL);
    client_remove_destroy_notify(ping_end);
}

static ObPingTarget* find_client(ObClient *client)
{
    GSList *it;
    for (it = ping_targets; it; it = g_slist_next(it))
        if (((ObPingTarget*)it->data)->client == client)
            return it->data;
    return NULL;
}

void ping_start(ObClient *client, ObPingEventHandler h)
{
    ObPingTarget *t;

    /* make sure the client supports ping! */
    if (!client->impl->ping) return;

    /* make sure we're not already pinging the client */
    if (find_client(client)) return;

    t = g_slice_new0(ObPingTarget);
    t->client = client;
    t->h = h;
    ping_targets = g_slist_prepend(ping_targets, t);

    /* act like we just timed out immediately, to start the pinging process
       now instead of after the first delay (this frees t if the client
       can't be pinged) */
    if (ping_timeout(t))
        t->loopid = g_timeout_add_full(G_PRIORITY_DEFAULT, PING_TIMEOUT,
                                       ping_timeout, t, NULL);
}

void ping_stop(ObClient *client)
{
    ping_end(client, NULL);
}

static gboolean ping_timeout(gpointer data)
{
    ObPingTarget *t = data;
    ObClient *c = t->client;

    if (t->waiting > 0 && !c->impl->ping_pending(c)) {
        /* it answered */
        if (t->waiting > PING_TIMEOUT_WARN)
            /* we had notified that they weren't responding, so now we
               need to notify that they are again */
            t->h(c, FALSE);
        /* we got a pong so we're happy now */
        t->loopid = 0;
        ping_end(c, NULL);
        return G_SOURCE_REMOVE;
    }

    if (!c->impl->ping(c)) {
        /* it can't be pinged */
        t->loopid = 0;
        ping_end(c, NULL);
        return G_SOURCE_REMOVE;
    }

    /* if the client hasn't been responding then do something about it */
    if (t->waiting == PING_TIMEOUT_WARN)
        t->h(c, TRUE); /* notify that the client isn't responding */

    ++t->waiting;

    return G_SOURCE_CONTINUE; /* repeat */
}

static void ping_end(ObClient *client, gpointer data)
{
    ObPingTarget *t;

    if ((t = find_client(client))) {
        ping_targets = g_slist_remove(ping_targets, t);
        if (t->loopid)
            g_source_remove(t->loopid);
        g_slice_free(ObPingTarget, t);
    }
}
