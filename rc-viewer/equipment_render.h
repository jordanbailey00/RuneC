// Viewer-only item/equipment render mapping.
//
// Gameplay item rules live in rc-core. This file only loads the generated
// cache-render map produced by tools/cache_pipeline/export_item_render_models.py.

#ifndef RUNEC_EQUIPMENT_RENDER_H
#define RUNEC_EQUIPMENT_RENDER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNEC_ITEM_RENDER_MAGIC 0x4D455249u /* "IREM" */
#define RUNEC_ITEM_RENDER_VERSION_1 1u
#define RUNEC_ITEM_RENDER_VERSION 2u
#define RUNEC_RENDER_BODY_PART_COUNT 7
#define RUNEC_RENDER_MODEL_MISSING 0xFFFFFFFFu

enum {
    RUNEC_BODY_HAIR = 0,
    RUNEC_BODY_JAW,
    RUNEC_BODY_TORSO,
    RUNEC_BODY_ARMS,
    RUNEC_BODY_HANDS,
    RUNEC_BODY_LEGS,
    RUNEC_BODY_FEET,
};

enum {
    RUNEC_BODY_MASK_HAIR  = 1u << RUNEC_BODY_HAIR,
    RUNEC_BODY_MASK_JAW   = 1u << RUNEC_BODY_JAW,
    RUNEC_BODY_MASK_TORSO = 1u << RUNEC_BODY_TORSO,
    RUNEC_BODY_MASK_ARMS  = 1u << RUNEC_BODY_ARMS,
    RUNEC_BODY_MASK_HANDS = 1u << RUNEC_BODY_HANDS,
    RUNEC_BODY_MASK_LEGS  = 1u << RUNEC_BODY_LEGS,
    RUNEC_BODY_MASK_FEET  = 1u << RUNEC_BODY_FEET,
};

enum {
    RUNEC_RENDER_ITEM_TWO_HANDED = 1u << 0,
    RUNEC_RENDER_ITEM_WEARPOS_AUTHORITY = 1u << 1,
};

typedef struct {
    uint32_t item_id;
    uint32_t ground_model_id;
    uint32_t male_model_id;
    uint32_t female_model_id;
    uint32_t hide_body_mask;
    uint32_t equip_slot;
    uint32_t wearpos1;
    uint32_t wearpos2;
    uint32_t wearpos3;
    uint32_t render_flags;
    uint32_t ready_anim_id;
    uint32_t walk_anim_id;
    uint32_t run_anim_id;
} RuneCItemRenderRecord;

typedef struct {
    uint32_t body_model_ids[RUNEC_RENDER_BODY_PART_COUNT];
    RuneCItemRenderRecord *records;
    int record_count;
    int loaded;
} RuneCItemRenderMap;

static int runec_read_u32(FILE *f, uint32_t *out) {
    return fread(out, sizeof(*out), 1, f) == 1;
}

static void runec_item_render_map_free(RuneCItemRenderMap *map) {
    if (!map) return;
    free(map->records);
    memset(map, 0, sizeof(*map));
}

static int runec_item_render_map_load(RuneCItemRenderMap *map, const char *path) {
    memset(map, 0, sizeof(*map));
    for (int i = 0; i < RUNEC_RENDER_BODY_PART_COUNT; i++)
        map->body_model_ids[i] = RUNEC_RENDER_MODEL_MISSING;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "item_render: can't open %s\n", path);
        return 0;
    }

    uint32_t magic, version, record_count, body_count;
    if (!runec_read_u32(f, &magic) || !runec_read_u32(f, &version)
            || !runec_read_u32(f, &record_count)
            || !runec_read_u32(f, &body_count)) {
        fclose(f);
        return 0;
    }
    if (magic != RUNEC_ITEM_RENDER_MAGIC
            || (version != RUNEC_ITEM_RENDER_VERSION
                && version != RUNEC_ITEM_RENDER_VERSION_1)
            || body_count > RUNEC_RENDER_BODY_PART_COUNT) {
        fprintf(stderr, "item_render: bad header in %s\n", path);
        fclose(f);
        return 0;
    }

    for (uint32_t i = 0; i < body_count; i++) {
        if (!runec_read_u32(f, &map->body_model_ids[i])) {
            fclose(f);
            return 0;
        }
    }

    map->records = calloc(record_count, sizeof(RuneCItemRenderRecord));
    if (!map->records) {
        fclose(f);
        return 0;
    }
    map->record_count = (int)record_count;

    for (uint32_t i = 0; i < record_count; i++) {
        RuneCItemRenderRecord *rec = &map->records[i];
        if (!runec_read_u32(f, &rec->item_id)
                || !runec_read_u32(f, &rec->ground_model_id)
                || !runec_read_u32(f, &rec->male_model_id)
                || !runec_read_u32(f, &rec->female_model_id)
                || !runec_read_u32(f, &rec->hide_body_mask)) {
            fclose(f);
            runec_item_render_map_free(map);
            return 0;
        }
        rec->equip_slot = RUNEC_RENDER_MODEL_MISSING;
        rec->wearpos1 = RUNEC_RENDER_MODEL_MISSING;
        rec->wearpos2 = RUNEC_RENDER_MODEL_MISSING;
        rec->wearpos3 = RUNEC_RENDER_MODEL_MISSING;
        rec->render_flags = 0;
        rec->ready_anim_id = RUNEC_RENDER_MODEL_MISSING;
        rec->walk_anim_id = RUNEC_RENDER_MODEL_MISSING;
        rec->run_anim_id = RUNEC_RENDER_MODEL_MISSING;
        if (version >= RUNEC_ITEM_RENDER_VERSION) {
            if (!runec_read_u32(f, &rec->equip_slot)
                    || !runec_read_u32(f, &rec->wearpos1)
                    || !runec_read_u32(f, &rec->wearpos2)
                    || !runec_read_u32(f, &rec->wearpos3)
                    || !runec_read_u32(f, &rec->render_flags)
                    || !runec_read_u32(f, &rec->ready_anim_id)
                    || !runec_read_u32(f, &rec->walk_anim_id)
                    || !runec_read_u32(f, &rec->run_anim_id)) {
                fclose(f);
                runec_item_render_map_free(map);
                return 0;
            }
        }
    }

    fclose(f);
    map->loaded = 1;
    fprintf(stderr, "item_render: loaded %d records from %s\n",
            map->record_count, path);
    return 1;
}

static const RuneCItemRenderRecord *runec_item_render_find(
    const RuneCItemRenderMap *map,
    uint32_t item_id
) {
    if (!map || !map->loaded) return NULL;
    for (int i = 0; i < map->record_count; i++) {
        if (map->records[i].item_id == item_id)
            return &map->records[i];
    }
    return NULL;
}

#endif /* RUNEC_EQUIPMENT_RENDER_H */
