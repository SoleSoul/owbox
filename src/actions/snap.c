#include "actions.h"
#include "client.h"
#include "snap.h"
#include "misc.h"

#include <wlr/util/edges.h>

/* owbox: snap the window to an edge or corner of its monitor, like dropping
   it there.  Snapping to the same place again puts it back.

   <action name="SnapToEdge"><direction>west</direction></action>
   <action name="Unsnap"/> */

typedef struct {
    guint edges;
} Options;

static gpointer setup_func(xmlNodePtr node);
static void free_func(gpointer o);
static gboolean run_func(ObActionsData *data, gpointer options);
static gboolean run_unsnap_func(ObActionsData *data, gpointer options);

void action_snap_startup(void)
{
    actions_register("SnapToEdge", setup_func, free_func, run_func);
    actions_register("Unsnap", NULL, NULL, run_unsnap_func);
}

static gpointer setup_func(xmlNodePtr node)
{
    xmlNodePtr n;
    Options *o;

    o = g_slice_new0(Options);
    o->edges = WLR_EDGE_LEFT;

    if ((n = obt_xml_find_node(node, "direction"))) {
        gchar *s = obt_xml_node_string(n);
        if (!g_ascii_strcasecmp(s, "north") ||
            !g_ascii_strcasecmp(s, "up"))
            o->edges = WLR_EDGE_TOP;
        else if (!g_ascii_strcasecmp(s, "south") ||
                 !g_ascii_strcasecmp(s, "down"))
            o->edges = WLR_EDGE_BOTTOM;
        else if (!g_ascii_strcasecmp(s, "east") ||
                 !g_ascii_strcasecmp(s, "right"))
            o->edges = WLR_EDGE_RIGHT;
        else if (!g_ascii_strcasecmp(s, "west") ||
                 !g_ascii_strcasecmp(s, "left"))
            o->edges = WLR_EDGE_LEFT;
        else if (!g_ascii_strcasecmp(s, "northwest"))
            o->edges = WLR_EDGE_TOP | WLR_EDGE_LEFT;
        else if (!g_ascii_strcasecmp(s, "northeast"))
            o->edges = WLR_EDGE_TOP | WLR_EDGE_RIGHT;
        else if (!g_ascii_strcasecmp(s, "southwest"))
            o->edges = WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
        else if (!g_ascii_strcasecmp(s, "southeast"))
            o->edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
        g_free(s);
    }
    return o;
}

static void free_func(gpointer o)
{
    g_slice_free(Options, o);
}

static gboolean run_func(ObActionsData *data, gpointer options)
{
    Options *o = options;

    if (data->client) {
        actions_client_move(data, TRUE);
        if (data->client->snap_edges == o->edges)
            snap_restore(data->client);
        else
            snap_client(data->client, o->edges, client_monitor(data->client));
        actions_client_move(data, FALSE);
    }
    return FALSE;
}

static gboolean run_unsnap_func(ObActionsData *data, gpointer options)
{
    if (data->client) {
        actions_client_move(data, TRUE);
        snap_restore(data->client);
        actions_client_move(data, FALSE);
    }
    return FALSE;
}
