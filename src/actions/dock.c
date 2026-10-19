#include "actions.h"

/* owbox has no dock (dockapps are an X thing, panels are layer shell
   surfaces), the dock actions are accepted and do nothing so Openbox
   configurations work unchanged */

static gboolean run_func(ObActionsData *data, gpointer options);

void action_dock_startup(void)
{
    actions_register("RaiseDock", NULL, NULL, run_func);
    actions_register("LowerDock", NULL, NULL, run_func);
}

static gboolean run_func(ObActionsData *data, gpointer options)
{
    return FALSE;
}
