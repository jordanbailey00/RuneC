#ifndef RC_DROPS_H
#define RC_DROPS_H

#include "types.h"

#include <stdint.h>

enum {
    RC_DROP_ALWAYS = 0,
    RC_DROP_MAIN = 1,
    RC_DROP_TERTIARY = 2,
};

enum {
    RC_SHARED_DROP_RDT = 0x5F544452u,
    RC_SHARED_DROP_GDT = 0x5F544447u,
    RC_SHARED_DROP_MRDT = 0x5444524Du,
};

typedef struct {
    uint32_t item_id;
    uint16_t qmin, qmax;
    uint32_t rarity_inv;
} RcDropEntry;

typedef struct {
    uint32_t npc_id;
    uint32_t rare_table_weight;
    uint32_t first[3];
    uint16_t count[3];
} RcDropTable;

#define RC_MAX_LOOT_DROPS 64

typedef struct {
    int item_id;
    int quantity;
    int kind;
} RcLootDrop;

extern RcDropTable *g_rc_drop_tables;
extern RcDropEntry *g_rc_drop_entries;
extern RcDropEntry *g_rc_rdt_entries;
extern RcDropEntry *g_rc_gdt_entries;
extern RcDropEntry *g_rc_mrdt_entries;
extern int g_rc_drop_table_count;
extern int g_rc_drop_entry_count;
extern int g_rc_rdt_entry_count;
extern int g_rc_gdt_entry_count;
extern int g_rc_mrdt_entry_count;

int rc_load_drops(const char *path);
int rc_load_rdt(const char *path);
int rc_load_gdt(const char *path);
int rc_load_mrdt(const char *path);

const RcDropTable *rc_drop_table_for_npc(int npc_id);
const RcDropEntry *rc_drop_entries_for(const RcDropTable *table, int kind,
                                       int *count);
const RcDropEntry *rc_shared_drop_entries(int kind, int *count);
int rc_roll_npc_loot(RcWorld *world, int npc_id, RcLootDrop *out, int max);

#endif
