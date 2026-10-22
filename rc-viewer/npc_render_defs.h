#ifndef RUNEC_NPC_RENDER_DEFS_H
#define RUNEC_NPC_RENDER_DEFS_H

#include "../rc-core/types.h"

#include <stdint.h>

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

// A definition resize (128 = 1.0). Models are exported unscaled and the
// viewer applies it after posing, as the client does.
typedef struct {
    int npc_id;
    uint16_t resize_h;
    uint16_t resize_v;
} RuneCNpcScale;

typedef struct {
    RuneCNpcRenderDef defs[RC_MAX_NPC_DEFS];
    int count;
    int by_id[RC_MAX_NPC_ID];
    RuneCNpcScale *scales;   // heap, sorted by npc_id; only non-default rows
    int scale_count;
    int loaded;
} RuneCNpcRenderDefs;

void runec_npc_render_defs_init(RuneCNpcRenderDefs *defs);
void runec_npc_render_defs_free(RuneCNpcRenderDefs *defs);
int runec_npc_render_defs_load(RuneCNpcRenderDefs *defs, const char *path);
int runec_npc_render_defs_apply_attack_anims(RuneCNpcRenderDefs *defs,
                                              const char *path);
// Loads `npcs.scales` (npc_id, resize_h, resize_v per line; `#` comments)
// written beside `npcs.models` by tools/export_npc_models_full.py. Returns
// the rows loaded, or -1 when the file is missing or malformed.
int runec_npc_render_defs_apply_scales(RuneCNpcRenderDefs *defs,
                                       const char *path);
// The NPC's resize; 128/128 when it has none.
void runec_npc_render_scale(const RuneCNpcRenderDefs *defs, int npc_id,
                            int *resize_h, int *resize_v);
const RuneCNpcRenderDef *runec_npc_render_find(
    const RuneCNpcRenderDefs *defs, int npc_id);

#endif
