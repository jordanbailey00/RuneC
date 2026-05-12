#include "drops.h"
#include "io.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>

#define DROP_MAGIC 0x504F5244u
#define DROP_VERSION 1u

RcDropTable *g_rc_drop_tables = NULL;
RcDropEntry *g_rc_drop_entries = NULL;
RcDropEntry *g_rc_rdt_entries = NULL;
RcDropEntry *g_rc_gdt_entries = NULL;
RcDropEntry *g_rc_mrdt_entries = NULL;
int g_rc_drop_table_count = 0;
int g_rc_drop_entry_count = 0;
int g_rc_rdt_entry_count = 0;
int g_rc_gdt_entry_count = 0;
int g_rc_mrdt_entry_count = 0;

static int g_drop_table_by_npc[RC_MAX_NPC_ID];

static void reset_drop_index(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_drop_table_by_npc[i] = -1;
}

static int read_header(FILE *f, const char *path, uint32_t magic,
                       uint32_t *count) {
    uint32_t got_magic, version;
    if (!rc_read_exact(f, &got_magic, sizeof(got_magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "version")
            || !rc_read_exact(f, count, sizeof(*count), 1, path, "count")) {
        return 0;
    }
    return got_magic == magic && version == DROP_VERSION;
}

static int append_entry(RcDropEntry **rows, int *count, int *cap,
                        RcDropEntry row) {
    if (*count >= *cap) {
        int next_cap = *cap ? *cap * 2 : 256;
        RcDropEntry *next = realloc(*rows, (size_t)next_cap * sizeof(**rows));
        if (!next) return 0;
        *rows = next;
        *cap = next_cap;
    }
    (*rows)[(*count)++] = row;
    return 1;
}

static int read_entry(FILE *f, const char *path, RcDropEntry *row,
                      int has_rarity) {
    row->rarity_inv = has_rarity ? 0 : 1;
    return rc_read_exact(f, &row->item_id, sizeof(row->item_id), 1, path,
                         "item id")
        && rc_read_exact(f, &row->qmin, sizeof(row->qmin), 1, path, "qmin")
        && rc_read_exact(f, &row->qmax, sizeof(row->qmax), 1, path, "qmax")
        && (!has_rarity
            || rc_read_exact(f, &row->rarity_inv, sizeof(row->rarity_inv), 1,
                             path, "rarity"));
}

int rc_load_drops(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, DROP_MAGIC, &count)) {
        fclose(f);
        return -1;
    }

    RcDropTable *tables = calloc(count ? count : 1u, sizeof(*tables));
    RcDropEntry *entries = NULL;
    int entry_count = 0, entry_cap = 0;
    if (!tables) {
        fclose(f);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        RcDropTable *table = &tables[i];
        uint8_t n;
        if (!rc_read_exact(f, &table->npc_id, sizeof(table->npc_id), 1, path,
                           "npc id")) {
            free(tables); free(entries); fclose(f); return -1;
        }
        for (int kind = RC_DROP_ALWAYS; kind <= RC_DROP_TERTIARY; kind++) {
            if (!rc_read_exact(f, &n, sizeof(n), 1, path, "drop count")) {
                free(tables); free(entries); fclose(f); return -1;
            }
            table->first[kind] = (uint32_t)entry_count;
            table->count[kind] = n;
            for (int j = 0; j < n; j++) {
                RcDropEntry row;
                if (!read_entry(f, path, &row, kind != RC_DROP_ALWAYS)
                        || !append_entry(&entries, &entry_count, &entry_cap,
                                         row)) {
                    free(tables); free(entries); fclose(f); return -1;
                }
            }
        }
        if (!rc_read_exact(f, &table->rare_table_weight,
                           sizeof(table->rare_table_weight), 1, path,
                           "rare table weight")) {
            free(tables); free(entries); fclose(f); return -1;
        }
    }
    fclose(f);

    free(g_rc_drop_tables);
    free(g_rc_drop_entries);
    g_rc_drop_tables = tables;
    g_rc_drop_entries = entries;
    g_rc_drop_table_count = (int)count;
    g_rc_drop_entry_count = entry_count;
    reset_drop_index();
    for (int i = 0; i < g_rc_drop_table_count; i++) {
        uint32_t npc_id = g_rc_drop_tables[i].npc_id;
        if (npc_id < RC_MAX_NPC_ID) g_drop_table_by_npc[npc_id] = i;
    }
    return g_rc_drop_table_count;
}

static int load_shared(const char *path, uint32_t magic, RcDropEntry **dst,
                       int *dst_count) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, magic, &count)) {
        fclose(f);
        return -1;
    }
    RcDropEntry *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        fclose(f);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!read_entry(f, path, &rows[i], 1)) {
            free(rows);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    free(*dst);
    *dst = rows;
    *dst_count = (int)count;
    return *dst_count;
}

int rc_load_rdt(const char *path) {
    return load_shared(path, RC_SHARED_DROP_RDT, &g_rc_rdt_entries,
                       &g_rc_rdt_entry_count);
}

int rc_load_gdt(const char *path) {
    return load_shared(path, RC_SHARED_DROP_GDT, &g_rc_gdt_entries,
                       &g_rc_gdt_entry_count);
}

int rc_load_mrdt(const char *path) {
    return load_shared(path, RC_SHARED_DROP_MRDT, &g_rc_mrdt_entries,
                       &g_rc_mrdt_entry_count);
}

const RcDropTable *rc_drop_table_for_npc(int npc_id) {
    if (npc_id < 0 || npc_id >= RC_MAX_NPC_ID || g_rc_drop_table_count <= 0) {
        return NULL;
    }
    int idx = g_drop_table_by_npc[npc_id];
    return idx >= 0 ? &g_rc_drop_tables[idx] : NULL;
}

const RcDropEntry *rc_drop_entries_for(const RcDropTable *table, int kind,
                                       int *count) {
    if (count) *count = 0;
    if (!table || kind < RC_DROP_ALWAYS || kind > RC_DROP_TERTIARY) {
        return NULL;
    }
    if (count) *count = table->count[kind];
    return &g_rc_drop_entries[table->first[kind]];
}

const RcDropEntry *rc_shared_drop_entries(int kind, int *count) {
    if (count) *count = 0;
    if (kind == RC_SHARED_DROP_RDT) {
        if (count) *count = g_rc_rdt_entry_count;
        return g_rc_rdt_entries;
    }
    if (kind == RC_SHARED_DROP_GDT) {
        if (count) *count = g_rc_gdt_entry_count;
        return g_rc_gdt_entries;
    }
    if (kind == RC_SHARED_DROP_MRDT) {
        if (count) *count = g_rc_mrdt_entry_count;
        return g_rc_mrdt_entries;
    }
    return NULL;
}

static int drop_quantity(RcWorld *world, const RcDropEntry *entry) {
    int min = (int)entry->qmin;
    int max = (int)entry->qmax;
    if (max < min) max = min;
    if (max == min) return min;
    return min + rc_rng_range(&world->rng_state, max - min);
}

static int drop_roll_succeeds(RcWorld *world, const RcDropEntry *entry) {
    uint32_t inv = entry->rarity_inv;
    if (inv == 0u) return 0;
    return inv == 1u || rc_rng_range(&world->rng_state, (int)inv - 1) == 0;
}

static uint32_t drop_weight(const RcDropEntry *entry) {
    if (entry->rarity_inv == 0u) return 0;
    uint32_t weight = 1000000u / entry->rarity_inv;
    return weight ? weight : 1u;
}

static int roll_main_entry(RcWorld *world, const RcDropEntry *entries,
                           int count) {
    uint64_t total = 0;
    for (int i = 0; i < count; i++) total += drop_weight(&entries[i]);
    if (total == 0) return -1;

    uint64_t roll = rc_rng_next(&world->rng_state) % total;
    for (int i = 0; i < count; i++) {
        uint32_t weight = drop_weight(&entries[i]);
        if (!weight) continue;
        if (roll < weight) return i;
        roll -= weight;
    }
    return -1;
}

static int append_loot(RcLootDrop *out, int max, int count,
                       const RcDropEntry *entry, int kind, int quantity) {
    if (!out || count >= max || entry->item_id == 0 || quantity <= 0) {
        return count;
    }
    out[count++] = (RcLootDrop){
        .item_id = (int)entry->item_id,
        .quantity = quantity,
        .kind = kind,
    };
    return count;
}

int rc_roll_npc_loot(RcWorld *world, int npc_id, RcLootDrop *out, int max) {
    if (!world || !out || max <= 0) return 0;
    const RcDropTable *table = rc_drop_table_for_npc(npc_id);
    if (!table) return 0;

    int total = 0;
    int n = 0;
    const RcDropEntry *entries =
        rc_drop_entries_for(table, RC_DROP_ALWAYS, &n);
    for (int i = 0; i < n && total < max; i++) {
        total = append_loot(out, max, total, &entries[i], RC_DROP_ALWAYS,
                            drop_quantity(world, &entries[i]));
    }

    entries = rc_drop_entries_for(table, RC_DROP_MAIN, &n);
    int main = roll_main_entry(world, entries, n);
    if (main >= 0 && total < max) {
        total = append_loot(out, max, total, &entries[main], RC_DROP_MAIN,
                            drop_quantity(world, &entries[main]));
    }

    entries = rc_drop_entries_for(table, RC_DROP_TERTIARY, &n);
    for (int i = 0; i < n && total < max; i++) {
        if (drop_roll_succeeds(world, &entries[i])) {
            total = append_loot(out, max, total, &entries[i],
                                RC_DROP_TERTIARY,
                                drop_quantity(world, &entries[i]));
        }
    }
    return total;
}
