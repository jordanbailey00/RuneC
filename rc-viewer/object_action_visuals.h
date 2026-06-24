#ifndef RUNEC_OBJECT_ACTION_VISUALS_H
#define RUNEC_OBJECT_ACTION_VISUALS_H

#include "../rc-core/objects.h"

typedef struct {
    int climb_anim;
    int loaded;
} RuneCObjectActionVisualRecord;

typedef struct {
    RuneCObjectActionVisualRecord records[RC_MAX_OBJECT_ID];
    int loaded;
} RuneCObjectActionVisualMap;

void runec_object_action_visuals_clear(RuneCObjectActionVisualMap *map);
int runec_object_action_visuals_load(RuneCObjectActionVisualMap *map,
                                     const char *path);
const RuneCObjectActionVisualRecord *runec_object_action_visual_find(
    const RuneCObjectActionVisualMap *map, int obj_id);

#endif
