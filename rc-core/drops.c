#include "drops.h"
#include "io.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static const RcDropTable *g_active_drop_tables = NULL;
static const RcDropEntry *g_active_drop_entries = NULL;
static const RcDropEntry *g_active_rdt_entries = NULL;
static const RcDropEntry *g_active_gdt_entries = NULL;
static const RcDropEntry *g_active_mrdt_entries = NULL;
static const int *g_active_drop_table_by_npc = g_drop_table_by_npc;
static int g_active_drop_table_count = 0;
static int g_active_drop_entry_count = 0;
static int g_active_rdt_entry_count = 0;
static int g_active_gdt_entry_count = 0;
static int g_active_mrdt_entry_count = 0;

static void reset_drop_index(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_drop_table_by_npc[i] = -1;
}

static void reset_drop_index_into(int *index) {
    if (!index) return;
    for (int i = 0; i < RC_MAX_NPC_ID; i++) index[i] = -1;
}

static void build_drop_index(const RcDropTable *tables, int table_count,
                             int *index) {
    reset_drop_index_into(index);
    if (!tables || !index) return;
    for (int i = 0; i < table_count; i++) {
        uint32_t npc_id = tables[i].npc_id;
        if (npc_id < RC_MAX_NPC_ID) index[npc_id] = i;
    }
}

void rc_drop_data_init(RcDropData *data) {
    if (!data) return;
    data->tables = NULL;
    data->entries = NULL;
    data->rdt_entries = NULL;
    data->gdt_entries = NULL;
    data->mrdt_entries = NULL;
    data->table_count = 0;
    data->entry_count = 0;
    data->rdt_entry_count = 0;
    data->gdt_entry_count = 0;
    data->mrdt_entry_count = 0;
    reset_drop_index_into(data->table_by_npc);
}

void rc_drop_data_free(RcDropData *data) {
    if (!data) return;
    free(data->tables);
    free(data->entries);
    free(data->rdt_entries);
    free(data->gdt_entries);
    free(data->mrdt_entries);
    rc_drop_data_init(data);
}

void rc_drops_use_data(const RcDropData *data) {
    if (!data) {
        g_active_drop_tables = g_rc_drop_tables;
        g_active_drop_entries = g_rc_drop_entries;
        g_active_rdt_entries = g_rc_rdt_entries;
        g_active_gdt_entries = g_rc_gdt_entries;
        g_active_mrdt_entries = g_rc_mrdt_entries;
        g_active_drop_table_by_npc = g_drop_table_by_npc;
        g_active_drop_table_count = g_rc_drop_table_count;
        g_active_drop_entry_count = g_rc_drop_entry_count;
        g_active_rdt_entry_count = g_rc_rdt_entry_count;
        g_active_gdt_entry_count = g_rc_gdt_entry_count;
        g_active_mrdt_entry_count = g_rc_mrdt_entry_count;
        return;
    }
    g_active_drop_tables = data->tables;
    g_active_drop_entries = data->entries;
    g_active_rdt_entries = data->rdt_entries;
    g_active_gdt_entries = data->gdt_entries;
    g_active_mrdt_entries = data->mrdt_entries;
    g_active_drop_table_by_npc = data->table_by_npc;
    g_active_drop_table_count = data->table_count;
    g_active_drop_entry_count = data->entry_count;
    g_active_rdt_entry_count = data->rdt_entry_count;
    g_active_gdt_entry_count = data->gdt_entry_count;
    g_active_mrdt_entry_count = data->mrdt_entry_count;
}

void rc_drops_reset_data_if_active(const RcDropData *data) {
    if (!data) return;
    if (g_active_drop_tables == data->tables
            || g_active_drop_entries == data->entries
            || g_active_rdt_entries == data->rdt_entries
            || g_active_gdt_entries == data->gdt_entries
            || g_active_mrdt_entries == data->mrdt_entries) {
        rc_drops_use_data(NULL);
    }
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

int rc_load_drops_into(const char *path, RcDropData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, DROP_MAGIC, &count)) {
        rc_asset_close(f);
        return -1;
    }

    RcDropTable *tables = calloc(count ? count : 1u, sizeof(*tables));
    RcDropEntry *entries = NULL;
    int entry_count = 0, entry_cap = 0;
    if (!tables) {
        rc_asset_close(f);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        RcDropTable *table = &tables[i];
        uint8_t n;
        if (!rc_read_exact(f, &table->npc_id, sizeof(table->npc_id), 1, path,
                           "npc id")) {
            free(tables); free(entries); rc_asset_close(f); return -1;
        }
        for (int kind = RC_DROP_ALWAYS; kind <= RC_DROP_TERTIARY; kind++) {
            if (!rc_read_exact(f, &n, sizeof(n), 1, path, "drop count")) {
                free(tables); free(entries); rc_asset_close(f); return -1;
            }
            table->first[kind] = (uint32_t)entry_count;
            table->count[kind] = n;
            for (int j = 0; j < n; j++) {
                RcDropEntry row;
                if (!read_entry(f, path, &row, kind != RC_DROP_ALWAYS)
                        || !append_entry(&entries, &entry_count, &entry_cap,
                                         row)) {
                    free(tables); free(entries); rc_asset_close(f); return -1;
                }
            }
        }
        if (!rc_read_exact(f, &table->rare_table_weight,
                           sizeof(table->rare_table_weight), 1, path,
                           "rare table weight")) {
            free(tables); free(entries); rc_asset_close(f); return -1;
        }
    }
    rc_asset_close(f);

    free(data->tables);
    free(data->entries);
    data->tables = tables;
    data->entries = entries;
    data->table_count = (int)count;
    data->entry_count = entry_count;
    build_drop_index(data->tables, data->table_count, data->table_by_npc);
    return data->table_count;
}

int rc_drops_mirror_to_globals(const RcDropData *data) {
    if (!data) return 0;
    RcDropTable *tables = NULL;
    RcDropEntry *entries = NULL;
    RcDropEntry *rdt = NULL;
    RcDropEntry *gdt = NULL;
    RcDropEntry *mrdt = NULL;

    if (data->table_count > 0) {
        if (!data->tables) goto fail;
        tables = malloc((size_t)data->table_count * sizeof(*tables));
        if (!tables) goto fail;
        memcpy(tables, data->tables,
               (size_t)data->table_count * sizeof(*tables));
    }
    if (data->entry_count > 0) {
        if (!data->entries) goto fail;
        entries = malloc((size_t)data->entry_count * sizeof(*entries));
        if (!entries) goto fail;
        memcpy(entries, data->entries,
               (size_t)data->entry_count * sizeof(*entries));
    }
    if (data->rdt_entry_count > 0) {
        if (!data->rdt_entries) goto fail;
        rdt = malloc((size_t)data->rdt_entry_count * sizeof(*rdt));
        if (!rdt) goto fail;
        memcpy(rdt, data->rdt_entries,
               (size_t)data->rdt_entry_count * sizeof(*rdt));
    }
    if (data->gdt_entry_count > 0) {
        if (!data->gdt_entries) goto fail;
        gdt = malloc((size_t)data->gdt_entry_count * sizeof(*gdt));
        if (!gdt) goto fail;
        memcpy(gdt, data->gdt_entries,
               (size_t)data->gdt_entry_count * sizeof(*gdt));
    }
    if (data->mrdt_entry_count > 0) {
        if (!data->mrdt_entries) goto fail;
        mrdt = malloc((size_t)data->mrdt_entry_count * sizeof(*mrdt));
        if (!mrdt) goto fail;
        memcpy(mrdt, data->mrdt_entries,
               (size_t)data->mrdt_entry_count * sizeof(*mrdt));
    }

    free(g_rc_drop_tables);
    free(g_rc_drop_entries);
    free(g_rc_rdt_entries);
    free(g_rc_gdt_entries);
    free(g_rc_mrdt_entries);
    g_rc_drop_tables = tables;
    g_rc_drop_entries = entries;
    g_rc_rdt_entries = rdt;
    g_rc_gdt_entries = gdt;
    g_rc_mrdt_entries = mrdt;
    g_rc_drop_table_count = data->table_count;
    g_rc_drop_entry_count = data->entry_count;
    g_rc_rdt_entry_count = data->rdt_entry_count;
    g_rc_gdt_entry_count = data->gdt_entry_count;
    g_rc_mrdt_entry_count = data->mrdt_entry_count;
    build_drop_index(g_rc_drop_tables, g_rc_drop_table_count,
                     g_drop_table_by_npc);
    return 1;

fail:
    free(tables);
    free(entries);
    free(rdt);
    free(gdt);
    free(mrdt);
    return 0;
}

int rc_load_drops(const char *path) {
    RcDropData data;
    rc_drop_data_init(&data);
    int loaded = rc_load_drops_into(path, &data);
    if (loaded < 0) return -1;
    if (!rc_drops_mirror_to_globals(&data)) {
        rc_drop_data_free(&data);
        return -1;
    }
    rc_drop_data_free(&data);
    rc_drops_use_data(NULL);
    return g_rc_drop_table_count;
}

static int load_shared_into(const char *path, uint32_t magic,
                            RcDropEntry **dst, int *dst_count) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, magic, &count)) {
        rc_asset_close(f);
        return -1;
    }
    RcDropEntry *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!read_entry(f, path, &rows[i], 1)) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
    }
    rc_asset_close(f);
    free(*dst);
    *dst = rows;
    *dst_count = (int)count;
    return *dst_count;
}

static int load_shared_global(const char *path, uint32_t magic,
                              RcDropEntry **dst, int *dst_count) {
    int loaded = load_shared_into(path, magic, dst, dst_count);
    if (loaded >= 0) rc_drops_use_data(NULL);
    return loaded;
}

int rc_load_rdt(const char *path) {
    return load_shared_global(path, RC_SHARED_DROP_RDT, &g_rc_rdt_entries,
                              &g_rc_rdt_entry_count);
}

int rc_load_gdt(const char *path) {
    return load_shared_global(path, RC_SHARED_DROP_GDT, &g_rc_gdt_entries,
                              &g_rc_gdt_entry_count);
}

int rc_load_mrdt(const char *path) {
    return load_shared_global(path, RC_SHARED_DROP_MRDT, &g_rc_mrdt_entries,
                              &g_rc_mrdt_entry_count);
}

int rc_load_rdt_into(const char *path, RcDropData *data) {
    return data ? load_shared_into(path, RC_SHARED_DROP_RDT,
                                   &data->rdt_entries,
                                   &data->rdt_entry_count) : -1;
}

int rc_load_gdt_into(const char *path, RcDropData *data) {
    return data ? load_shared_into(path, RC_SHARED_DROP_GDT,
                                   &data->gdt_entries,
                                   &data->gdt_entry_count) : -1;
}

int rc_load_mrdt_into(const char *path, RcDropData *data) {
    return data ? load_shared_into(path, RC_SHARED_DROP_MRDT,
                                   &data->mrdt_entries,
                                   &data->mrdt_entry_count) : -1;
}

const RcDropTable *rc_drop_table_for_npc(int npc_id) {
    if (npc_id < 0 || npc_id >= RC_MAX_NPC_ID
            || g_active_drop_table_count <= 0
            || !g_active_drop_tables || !g_active_drop_table_by_npc) {
        return NULL;
    }
    int idx = g_active_drop_table_by_npc[npc_id];
    return idx >= 0 ? &g_active_drop_tables[idx] : NULL;
}

const RcDropEntry *rc_drop_entries_for(const RcDropTable *table, int kind,
                                       int *count) {
    if (count) *count = 0;
    if (!table || kind < RC_DROP_ALWAYS || kind > RC_DROP_TERTIARY) {
        return NULL;
    }
    if (count) *count = table->count[kind];
    if (!g_active_drop_entries) return NULL;
    return &g_active_drop_entries[table->first[kind]];
}

const RcDropEntry *rc_shared_drop_entries(int kind, int *count) {
    if (count) *count = 0;
    if (kind == RC_SHARED_DROP_RDT) {
        if (count) *count = g_active_rdt_entry_count;
        return g_active_rdt_entries;
    }
    if (kind == RC_SHARED_DROP_GDT) {
        if (count) *count = g_active_gdt_entry_count;
        return g_active_gdt_entries;
    }
    if (kind == RC_SHARED_DROP_MRDT) {
        if (count) *count = g_active_mrdt_entry_count;
        return g_active_mrdt_entries;
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
