#include "actions.h"
#include "panels.h"

/* owbox: show the panels above the dimmed windows, and run a launcher, or
   hide them if they are shown.  See panels.c.

   <action name="TogglePanels"><launcher>fuzzel</launcher></action> */

typedef struct {
    gchar *launcher;
} Options;

static gpointer setup_func(xmlNodePtr node);
static void     free_func(gpointer options);
static gboolean run_func(ObActionsData *data, gpointer options);

void action_togglepanels_startup(void)
{
    actions_register("TogglePanels", setup_func, free_func, run_func);
}

static gpointer setup_func(xmlNodePtr node)
{
    xmlNodePtr n;
    Options *o = g_slice_new0(Options);

    if ((n = obt_xml_find_node(node, "launcher"))) {
        o->launcher = obt_xml_node_string(n);
        if (!*o->launcher) {
            g_free(o->launcher);
            o->launcher = NULL;
        }
    }
    return o;
}

static void free_func(gpointer options)
{
    Options *o = options;
    g_free(o->launcher);
    g_slice_free(Options, o);
}

static gboolean run_func(ObActionsData *data, gpointer options)
{
    Options *o = options;

    if (panels_shown())
        panels_hide();
    else
        panels_show(o->launcher);
    return FALSE;
}
