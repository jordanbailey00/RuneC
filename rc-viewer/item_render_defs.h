#ifndef RUNEC_ITEM_RENDER_DEFS_H
#define RUNEC_ITEM_RENDER_DEFS_H

#include "../rc-core/types.h"
#include "equipment_render.h"

typedef struct {
    uint32_t ground_model_id;
    uint32_t male_model_ids[3];
    uint32_t female_model_ids[3];
    int loaded;
} RuneCItemDefRenderRecord;

typedef struct {
    RuneCItemDefRenderRecord *records;
    int loaded;
} RuneCItemDefRenderMap;

void runec_item_def_render_map_free(RuneCItemDefRenderMap *map);
int runec_item_def_render_map_load(RuneCItemDefRenderMap *map,
                                   const char *path);
const RuneCItemDefRenderRecord *runec_item_def_render_find(
    const RuneCItemDefRenderMap *map, int item_id);

#endif
