#ifndef RUNEC_NPC_RENDER_DEFS_H
#define RUNEC_NPC_RENDER_DEFS_H

#include "../rc-core/types.h"

#define RUNEC_NPC_RENDER_MAX_MODELS 12

typedef struct {
    int id;
    int stand_anim;
    int walk_anim;
    int run_anim;
    int attack_anim;
    int death_anim;
    int model_count;
    int model_ids[RUNEC_NPC_RENDER_MAX_MODELS];
    int loaded;
} RuneCNpcRenderDef;

typedef struct {
    RuneCNpcRenderDef defs[RC_MAX_NPC_DEFS];
    int count;
    int by_id[RC_MAX_NPC_ID];
    int loaded;
} RuneCNpcRenderDefs;

void runec_npc_render_defs_init(RuneCNpcRenderDefs *defs);
int runec_npc_render_defs_load(RuneCNpcRenderDefs *defs, const char *path);
const RuneCNpcRenderDef *runec_npc_render_find(
    const RuneCNpcRenderDefs *defs, int npc_id);

#endif
