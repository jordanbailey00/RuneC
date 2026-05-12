#ifndef RC_VARBITS_H
#define RC_VARBITS_H

#include "types.h"

#include <stdint.h>

typedef struct {
    char name[64];
    uint16_t base_varp;
    uint8_t lsb, msb, loaded;
} RcVarbitDef;

typedef struct {
    char name[32];
    uint16_t type;
    uint8_t loaded;
} RcVarpDef;

extern RcVarbitDef g_rc_varbits[RC_MAX_VARBITS];
extern RcVarpDef g_rc_varps[RC_MAX_VARPS];
extern int g_rc_varbit_count;
extern int g_rc_varp_count;

int rc_load_varbits(const char *path);
int rc_load_varps(const char *path);
const RcVarbitDef *rc_varbit_def_get(int varbit_id);
const RcVarpDef *rc_varp_def_get(int varp_id);
int rc_varbit_find(const char *name);
int rc_varp_find(const char *name);
uint32_t rc_varbit_get(const RcWorld *world, int varbit_id);
int rc_varbit_set(RcWorld *world, int varbit_id, uint32_t value);

#endif
