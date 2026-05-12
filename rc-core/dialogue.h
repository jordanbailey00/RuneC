#ifndef RC_DIALOGUE_H
#define RC_DIALOGUE_H

#include "types.h"

#include <stdint.h>

typedef enum {
    DIALOGUE_NONE,
    DIALOGUE_NPC_CHAT,
    DIALOGUE_PLAYER_CHAT,
    DIALOGUE_OPTIONS,
    DIALOGUE_ITEM_GIVEN,
} RcDialogueType;

typedef struct {
    RcDialogueType type;
    int npc_id;
    char text[256];
    char options[5][64];
    int option_count;
    int next_state[5];      // state after each option (or after continue)
} RcDialogueNode;

// Current dialogue state (part of world, not player, since it's UI state)
typedef struct {
    bool active;
    int npc_uid;
    int node_index;
    const RcDialogueNode *nodes;
    int node_count;
} RcDialogueState;

typedef struct {
    uint16_t id;
    int16_t parent;
    uint8_t depth, kind, is_terminal;
    uint16_t child_count;
    uint32_t first_child;
    uint32_t speaker_off;
    uint32_t text_off;
} RcDialogueDefNode;

typedef struct {
    char slug[64];
    uint32_t first_node;
    uint16_t node_count;
    uint32_t first_npc;
    uint8_t npc_count;
} RcDialogueTranscriptDef;

extern RcDialogueTranscriptDef *g_rc_dialogue_transcripts;
extern RcDialogueDefNode *g_rc_dialogue_nodes;
extern uint16_t *g_rc_dialogue_child_ids;
extern uint32_t *g_rc_dialogue_npc_name_offsets;
extern int g_rc_dialogue_transcript_count;
extern int g_rc_dialogue_node_count;
extern int g_rc_dialogue_child_id_count;

int rc_load_dialogue(const char *path);
int rc_dialogue_find_transcript(const char *slug);
const RcDialogueTranscriptDef *rc_dialogue_transcript_get(int idx);
const RcDialogueDefNode *rc_dialogue_nodes_for(
    const RcDialogueTranscriptDef *transcript, int *count);
const char *rc_dialogue_string(uint32_t offset);
const char *rc_dialogue_transcript_npc(const RcDialogueTranscriptDef *row,
                                       int idx);

void rc_dialogue_start(RcWorld *world, int npc_uid, const RcDialogueNode *nodes, int count);
void rc_dialogue_continue(RcWorld *world);
void rc_dialogue_choose(RcWorld *world, int option);

#endif
