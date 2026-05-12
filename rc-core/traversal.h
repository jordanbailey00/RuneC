#ifndef RC_TRAVERSAL_H
#define RC_TRAVERSAL_H

#include "types.h"

#define RC_TRAVERSAL_MAX_SOURCE_ID 65536

enum {
    RC_TRAVERSAL_OBJECT = 1,
    RC_TRAVERSAL_ITEM = 2,
    RC_TRAVERSAL_SPELL = 3,
    RC_TRAVERSAL_KIND_COUNT = 4,
};

typedef struct {
    uint8_t kind, option;
    uint8_t start_plane, dest_plane;
    uint32_t source_id, flags;
    uint16_t start_x, start_y, dest_x, dest_y;
    char action[32];
    char target[48];
} RcTraversalEdge;

extern RcTraversalEdge *g_rc_traversal_edges;
extern int g_rc_traversal_edge_count;

int rc_load_traversal_edges(const char *path);
const RcTraversalEdge *rc_traversal_edges_for(int kind, int source_id,
                                              int *count);
const RcTraversalEdge *rc_traversal_find(int kind, int source_id,
                                         int x, int y, int plane, int option);
const RcTraversalEdge *rc_traversal_find_target(int kind, const char *target);
int rc_player_apply_traversal(RcWorld *world, const RcTraversalEdge *edge);

#endif
