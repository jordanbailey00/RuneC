#include "content.h"
#include "config.h"
#include "storage.h"

#include "storage_policy.inc"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int in_ranges(int id, const int ranges[][2], int count) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (id < ranges[mid][0]) hi = mid;
        else if (id > ranges[mid][1]) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static int item_bankable(int id) {
    if (id < 0 || id > STORAGE_REVIEWED_ITEM_MAX
            || in_ranges(id, unknown_items, COUNT(unknown_items))) return -1;
    return !in_ranges(id, unbankable, COUNT(unbankable));
}

int rc_content_storage_register(RcWorld *world) {
    static const RcStoragePolicy policy = {
        object_storage, npc_storage, COUNT(object_storage), COUNT(npc_storage),
        item_bankable,
    };
    if (!world) return 0;
    if (world->enabled & RC_SUB_STORAGE) world->storage_policy = &policy;
    return 1;
}
