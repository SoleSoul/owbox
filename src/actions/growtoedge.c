#include "actions.h"
#include "misc.h"
#include "client.h"
#include "frame.h"
#include "screen.h"
#include <glib.h>

typedef struct {
    ObDirection dir;
    gboolean shrink;
    gboolean fill;
} Options;

/* owbox: a Wayland client may choose a smaller size than it is asked for,
   and a terminal rounds it down to whole cells, without telling its size
   increments as X clients did.  Openbox skips the edges nearer than a size
   increment, so a grow that the client stopped short of becomes the
   increment for the next grow in the same direction, which goes on to the
   next edge instead of asking for the same one again (labwc does the same,
   in its snap-constraints.c) */
static struct {
    ObClient *client;
    ObDirection dir;
    Rect before; /* the client's area before the grow */
    Rect asked;  /* the area the grow asked for */
} last_grow;

static gpointer setup_grow_func(xmlNodePtr node);
static gpointer setup_fill_func(xmlNodePtr node);
static gpointer setup_shrink_func(xmlNodePtr node);
static void free_func(gpointer o);
static gboolean run_func(ObActionsData *data, gpointer options);
/* 3.4-compatibility */
static gpointer setup_north_func(xmlNodePtr node);
static gpointer setup_south_func(xmlNodePtr node);
static gpointer setup_east_func(xmlNodePtr node);
static gpointer setup_west_func(xmlNodePtr node);
static void client_dest(ObClient *client, gpointer data);

void action_growtoedge_startup(void)
{
    actions_register("GrowToEdge", setup_grow_func,
                     free_func, run_func);
    actions_register("GrowToFill", setup_fill_func,
                     free_func, run_func);
    actions_register("ShrinkToEdge", setup_shrink_func,
                     free_func, run_func);
    /* 3.4-compatibility */
    actions_register("GrowToEdgeNorth", setup_north_func, free_func, run_func);
    actions_register("GrowToEdgeSouth", setup_south_func, free_func, run_func);
    actions_register("GrowToEdgeEast", setup_east_func, free_func, run_func);
    actions_register("GrowToEdgeWest", setup_west_func, free_func, run_func);

    client_add_destroy_notify(client_dest, NULL);
}

static void client_dest(ObClient *client, gpointer data)
{
    if (last_grow.client == client)
        last_grow.client = NULL;
}

static gpointer setup_func(xmlNodePtr node)
{
    xmlNodePtr n;
    Options *o;

    o = g_slice_new0(Options);
    o->dir = OB_DIRECTION_NORTH;

    if ((n = obt_xml_find_node(node, "direction"))) {
        gchar *s = obt_xml_node_string(n);
        if (!g_ascii_strcasecmp(s, "north") ||
            !g_ascii_strcasecmp(s, "up"))
            o->dir = OB_DIRECTION_NORTH;
        else if (!g_ascii_strcasecmp(s, "south") ||
                 !g_ascii_strcasecmp(s, "down"))
            o->dir = OB_DIRECTION_SOUTH;
        else if (!g_ascii_strcasecmp(s, "west") ||
                 !g_ascii_strcasecmp(s, "left"))
            o->dir = OB_DIRECTION_WEST;
        else if (!g_ascii_strcasecmp(s, "east") ||
                 !g_ascii_strcasecmp(s, "right"))
            o->dir = OB_DIRECTION_EAST;
        g_free(s);
    }

    return o;
}

static gpointer setup_grow_func(xmlNodePtr node)
{
    Options *o;

    o = setup_func(node);
    o->shrink = FALSE;
    o->fill = FALSE;

    return o;
}

static gpointer setup_fill_func(xmlNodePtr node)
{
    Options *o;

    o = setup_func(node);
    o->shrink = FALSE;
    o->fill = TRUE;

    return o;
}

static gpointer setup_shrink_func(xmlNodePtr node)
{
    Options *o;

    o = setup_func(node);
    o->shrink = TRUE;
    o->fill = FALSE;

    return o;
}

static gboolean do_grow(ObActionsData *data, gint x, gint y, gint w, gint h)
{
    gint realw, realh, lw, lh;

    realw = w;
    realh = h;
    client_try_configure(data->client, &x, &y, &realw, &realh,
                         &lw, &lh, TRUE);
    /* if it's going to be resized smaller than it intended, don't
       move the window over */
    if (x != data->client->area.x) x += w - realw;
    if (y != data->client->area.y) y += h - realh;

    if (x != data->client->area.x || y != data->client->area.y ||
        realw != data->client->area.width ||
        realh != data->client->area.height)
    {
        actions_client_move(data, TRUE);
        client_move_resize(data->client, x, y, realw, realh);
        actions_client_move(data, FALSE);
        return TRUE;
    }
    return FALSE;
}

static gboolean do_grow_all_edges(ObActionsData* data,
                                  ObClientDirectionalResizeType resize_type)
{
    gint x, y, w, h;
    gint temp_x, temp_y, temp_w, temp_h;

    client_find_resize_directional(data->client,
                                   OB_DIRECTION_NORTH,
                                   resize_type,
                                   &temp_x, &temp_y, &temp_w, &temp_h);
    y = temp_y;
    h = temp_h;

    client_find_resize_directional(data->client,
                                   OB_DIRECTION_SOUTH,
                                   resize_type,
                                   &temp_x, &temp_y, &temp_w, &temp_h);
    h += temp_h - data->client->area.height;


    client_find_resize_directional(data->client,
                                   OB_DIRECTION_WEST,
                                   resize_type,
                                   &temp_x, &temp_y, &temp_w, &temp_h);
    x = temp_x;
    w = temp_w;

    client_find_resize_directional(data->client,
                                   OB_DIRECTION_EAST,
                                   resize_type,
                                   &temp_x, &temp_y, &temp_w, &temp_h);
    w += temp_w - data->client->area.width;

    /* When filling, we allow the window to move to an arbitrary x/y
       position, since we'll be growing the other edge as well. */
    int lw, lh;
    client_try_configure(data->client, &x, &y, &w, &h, &lw, &lh, TRUE);

    if (x == data->client->area.x &&
        y == data->client->area.y &&
        w == data->client->area.width &&
        h == data->client->area.height)
    {
        return FALSE;
    }

    actions_client_move(data, TRUE);
    client_move_resize(data->client, x, y, w, h);
    actions_client_move(data, FALSE);
    return TRUE;
}

static void free_func(gpointer o)
{
    g_slice_free(Options, o);
}

/*! owbox: how far the client stopped short of the last grow, plus one (an
  increment that skips the edge it was asked to reach), when it is still
  where it stopped, else 1.  horz says which increment is asked for. */
static gint short_of_last_grow(ObClient *c, ObDirection dir, gboolean horz)
{
    const Rect *a = &c->area, *b = &last_grow.before, *q = &last_grow.asked;

    if (c != last_grow.client || dir != last_grow.dir ||
        c->configure_pending)
        return 1;

    switch (dir) {
    case OB_DIRECTION_EAST:
        if (horz && a->x == q->x && a->y == q->y && a->height == q->height &&
            a->width >= b->width && a->width < q->width)
            return q->width - a->width + 1;
        break;
    case OB_DIRECTION_WEST:
        if (horz && RECT_RIGHT(*a) == RECT_RIGHT(*q) && a->y == q->y &&
            a->height == q->height &&
            a->width >= b->width && a->width < q->width)
            return q->width - a->width + 1;
        break;
    case OB_DIRECTION_SOUTH:
        if (!horz && a->y == q->y && a->x == q->x && a->width == q->width &&
            a->height >= b->height && a->height < q->height)
            return q->height - a->height + 1;
        break;
    case OB_DIRECTION_NORTH:
        if (!horz && RECT_BOTTOM(*a) == RECT_BOTTOM(*q) && a->x == q->x &&
            a->width == q->width &&
            a->height >= b->height && a->height < q->height)
            return q->height - a->height + 1;
        break;
    default:
        break;
    }
    return 1;
}

/* Always return FALSE because its not interactive */
static gboolean run_func(ObActionsData *data, gpointer options)
{
    Options *o = options;

    if (!data->client)
        return FALSE;

    gboolean doing_vertical_resize =
        o->dir == OB_DIRECTION_NORTH ||
        o->dir == OB_DIRECTION_SOUTH ||
        o->fill;
    if (data->client->shaded && doing_vertical_resize)
            return FALSE;

    if (o->fill) {
        if (o->shrink) {
            /* We don't have any implementation of shrinking for the FillToGrow
               action. */
            return FALSE;
        }

        if (do_grow_all_edges(data, CLIENT_RESIZE_GROW_IF_NOT_ON_EDGE))
            return FALSE;

        /* If all the edges are blocked, then allow them to jump past their
           current block points. */
        do_grow_all_edges(data, CLIENT_RESIZE_GROW);
        return FALSE;
    }

    if (!o->shrink) {
        gint x, y, w, h;
        ObClient *c = data->client;
        const Size inc = c->size_inc;
        const Rect before = c->area;

        /* owbox: the client stopped short of the last grow */
        c->size_inc.width =
            MAX(inc.width, short_of_last_grow(c, o->dir, TRUE));
        c->size_inc.height =
            MAX(inc.height, short_of_last_grow(c, o->dir, FALSE));

        /* Try grow. */
        client_find_resize_directional(data->client,
                                       o->dir,
                                       CLIENT_RESIZE_GROW,
                                       &x, &y, &w, &h);
        c->size_inc = inc;

        if (do_grow(data, x, y, w, h)) {
            last_grow.client = c;
            last_grow.dir = o->dir;
            last_grow.before = before;
            RECT_SET(last_grow.asked, x, y, w, h);
            client_try_configure(c, &last_grow.asked.x, &last_grow.asked.y,
                                 &last_grow.asked.width,
                                 &last_grow.asked.height, &w, &h, TRUE);
            return FALSE;
        }
    }

    /* We couldn't grow, so try shrink! */
    ObDirection opposite =
        (o->dir == OB_DIRECTION_NORTH ? OB_DIRECTION_SOUTH :
         (o->dir == OB_DIRECTION_SOUTH ? OB_DIRECTION_NORTH :
          (o->dir == OB_DIRECTION_EAST ? OB_DIRECTION_WEST :
           OB_DIRECTION_EAST)));

    gint x, y, w, h;
    gint half;

    client_find_resize_directional(data->client,
                                   opposite,
                                   CLIENT_RESIZE_SHRINK,
                                   &x, &y, &w, &h);

    switch (opposite) {
    case OB_DIRECTION_NORTH:
        half = data->client->area.y + data->client->area.height / 2;
        if (y > half) {
            h += y - half;
            y = half;
        }
        break;
    case OB_DIRECTION_SOUTH:
        half = data->client->area.height / 2;
        if (h < half)
            h = half;
        break;
    case OB_DIRECTION_WEST:
        half = data->client->area.x + data->client->area.width / 2;
        if (x > half) {
            w += x - half;
            x = half;
        }
        break;
    case OB_DIRECTION_EAST:
        half = data->client->area.width / 2;
        if (w < half)
            w = half;
        break;
    default: g_assert_not_reached();
    }
    if (do_grow(data, x, y, w, h))
        return FALSE;

    return FALSE;
}

/* 3.4-compatibility */
static gpointer setup_north_func(xmlNodePtr node)
{
    Options *o = g_slice_new0(Options);
    o->shrink = FALSE;
    o->dir = OB_DIRECTION_NORTH;
    return o;
}

static gpointer setup_south_func(xmlNodePtr node)
{
    Options *o = g_slice_new0(Options);
    o->shrink = FALSE;
    o->dir = OB_DIRECTION_SOUTH;
    return o;
}

static gpointer setup_east_func(xmlNodePtr node)
{
    Options *o = g_slice_new0(Options);
    o->shrink = FALSE;
    o->dir = OB_DIRECTION_EAST;
    return o;
}

static gpointer setup_west_func(xmlNodePtr node)
{
    Options *o = g_slice_new0(Options);
    o->shrink = FALSE;
    o->dir = OB_DIRECTION_WEST;
    return o;
}
