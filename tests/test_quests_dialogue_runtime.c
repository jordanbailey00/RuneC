#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "dialogue.h"
#include "quests.h"

#define QUEST_PATH RC_TEST_SOURCE_DIR "/data/defs/quests.bin"
#define DIALOGUE_PATH RC_TEST_SOURCE_DIR "/data/defs/dialogue.bin"
#define BAD_PATH "/tmp/runec_bad_quest_dialogue.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static void write_bad_dialogue_body(void) {
    uint32_t header[3] = {0x58474C44u, 1u, 1u};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(header, sizeof(header), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_quests(NULL) == -1);
    assert(rc_load_quests(BAD_PATH) == -1);
    assert(rc_load_dialogue(BAD_PATH) == -1);

    assert(rc_load_quests(QUEST_PATH) == 215);
    int ds2 = rc_quest_find("Dragon Slayer II");
    assert(ds2 >= 0);
    const RcQuestDef *quest = rc_quest_def_get(ds2);
    assert(quest != NULL);
    assert(quest->difficulty == 5);
    assert(quest->length == 5);
    assert(quest->req_count > 0);
    RcQuestProgress progress = {0};
    rc_quest_set_state(&progress, ds2, 100);
    assert(rc_quest_get_state(&progress, ds2) == 100);

    assert(rc_load_dialogue(DIALOGUE_PATH) == 380);
    assert(g_rc_dialogue_node_count > 150000);
    int idx = rc_dialogue_find_transcript("2013_Christmas_event");
    assert(idx >= 0);
    const RcDialogueTranscriptDef *transcript =
        rc_dialogue_transcript_get(idx);
    assert(transcript != NULL);
    assert(transcript->npc_count == 4);
    assert(strcmp(rc_dialogue_transcript_npc(transcript, 0),
                  "Shanty Claws") == 0);
    int node_count = 0;
    const RcDialogueDefNode *nodes =
        rc_dialogue_nodes_for(transcript, &node_count);
    assert(nodes != NULL && node_count == transcript->node_count);
    assert(rc_dialogue_string(nodes[0].text_off)[0] != '\0');
    write_bad_dialogue_body();
    assert(rc_load_dialogue(BAD_PATH) == -1);
    assert(strcmp(rc_dialogue_transcript_npc(transcript, 0),
                  "Shanty Claws") == 0);

    g_rc_quest_count = 0;
    g_rc_dialogue_transcript_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_QUESTS | RC_SUB_DIALOGUE;
    cfg.quests_path = QUEST_PATH;
    cfg.dialogue_path = DIALOGUE_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_quest_count == 215);
    assert(g_rc_dialogue_transcript_count == 380);
    rc_world_destroy(world);

    printf("test_quests_dialogue_runtime: quest/dialogue metadata loaded.\n");
    return 0;
}
