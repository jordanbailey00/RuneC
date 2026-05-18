#ifndef RUNEC_VIEWER_DEV_VALIDATION_H
#define RUNEC_VIEWER_DEV_VALIDATION_H

#include "../rc-core/types.h"

typedef struct RuneCDevTransport {
    const char *key;
    const char *label;
    int target_x, target_y, plane;
    int npc_id;
    int npc_size;
} RuneCDevTransport;

enum {
    RUNEC_DEV_BANK_TAB_RANGED = 0,
    RUNEC_DEV_BANK_TAB_MAGE = 1,
    RUNEC_DEV_BANK_TAB_MELEE = 2,
    RUNEC_DEV_BANK_TAB_PVP = 3,
    RUNEC_DEV_BANK_TAB_SPECIAL = 4,
};

enum {
    RUNEC_DEV_VARROCK_BANK_X = 3184,
    RUNEC_DEV_VARROCK_BANK_Y = 3440,
};

int runec_dev_validation_enabled(void);
const RuneCDevTransport *runec_dev_validation_transports(int *count);
const RuneCDevTransport *runec_dev_validation_find_transport(const char *key);
void runec_dev_validation_seed_bank(RcWorld *world);
int runec_dev_validation_spawn_varrock_bank_dummy(RcWorld *world);
int runec_dev_validation_bank_withdraw_quantity(const RcWorld *world,
                                                int bank_slot);

#endif
