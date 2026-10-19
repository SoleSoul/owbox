#include "actions.h"

/* owbox has no dock, see dock.c */

static gboolean run_func(ObActionsData *data, gpointer options);

void action_dockautohide_startup(void)
{
    actions_register("ToggleDockAutoHide", NULL, NULL, run_func);
}

static gboolean run_func(ObActionsData *data, gpointer options)
{
    return FALSE;
}
