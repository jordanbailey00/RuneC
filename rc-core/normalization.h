#ifndef RC_NORMALIZATION_H
#define RC_NORMALIZATION_H

#include "types.h"

#include <stdint.h>

enum {
    RC_NORM_ITEM_NOTED       = 1u << 0,
    RC_NORM_ITEM_PLACEHOLDER = 1u << 1,
    RC_NORM_ITEM_NOTEABLE    = 1u << 2,
    RC_NORM_ITEM_VARIANT     = 1u << 3,
};

enum {
    RC_NORM_NPC_ALIAS_GROUP = 1u << 0,
};

typedef struct {
    int canonical_id;
    int noted_id;
    int placeholder_id;
    uint32_t key_hash;
    uint16_t flags;
    uint8_t loaded;
} RcItemNormalization;

typedef struct {
    int canonical_id;
    uint32_t key_hash;
    uint16_t flags;
    uint8_t loaded;
} RcNpcNormalization;

typedef struct {
    uint32_t key_hash;
    uint32_t ref_id;
    uint8_t kind;
} RcSourceNormalization;

extern RcItemNormalization g_rc_item_normalization[RC_MAX_ITEM_DEFS];
extern RcNpcNormalization g_rc_npc_normalization[RC_MAX_NPC_ID];
extern RcSourceNormalization *g_rc_source_normalization;
extern int g_rc_item_normalization_count;
extern int g_rc_npc_normalization_count;
extern int g_rc_source_normalization_count;

int rc_load_normalization(const char *path);
int rc_load_normalization_into(const char *path,
                               RcItemNormalization *item_defs,
                               int max_item_defs, int *out_item_count,
                               RcNpcNormalization *npc_defs,
                               int max_npc_defs, int *out_npc_count,
                               RcSourceNormalization **source_defs,
                               int *out_source_count);
void rc_normalization_use_defs(const RcItemNormalization *item_defs,
                               int item_count,
                               const RcNpcNormalization *npc_defs,
                               int npc_count,
                               const RcSourceNormalization *source_defs,
                               int source_count);
void rc_normalization_reset_defs_if_active(
    const RcItemNormalization *item_defs,
    const RcNpcNormalization *npc_defs,
    const RcSourceNormalization *source_defs);
uint32_t rc_normalization_hash_key(const char *name);
int rc_normalize_item_id(int item_id);
int rc_item_noted_id(int item_id);
int rc_item_placeholder_id(int item_id);
int rc_normalize_npc_id(int npc_id);
int rc_normalization_find_source(uint8_t kind, uint32_t key_hash);

#endif
