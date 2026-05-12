#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "monster_mechanics.h"

#define RC_TEST_REGULAR_NPC_MECH_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/regular_npc_mechanics.bin"

static void reset_mechanics(void) {
    g_rc_monster_mechanic_family_count = 0;
}

int main(void) {
    reset_mechanics();
    int loaded = rc_load_monster_mechanics(RC_TEST_REGULAR_NPC_MECH_BIN);
    assert(loaded == 16);
    assert(g_rc_monster_mechanic_family_count == 16);

    int dragonfire = rc_monster_mechanics_find_family("dragonfire_users");
    int leafy = rc_monster_mechanics_find_family("weapon_restricted_damage");
    int finishers = rc_monster_mechanics_find_family("slayer_finisher_items");
    int poison = rc_monster_mechanics_find_family("poison_users");
    int venom = rc_monster_mechanics_find_family("venom_users");
    int disease = rc_monster_mechanics_find_family("disease_users");
    int mirror = rc_monster_mechanics_find_family("mirror_shield_required");
    int spectre = rc_monster_mechanics_find_family(
        "spectre_protection_required");
    int revenants = rc_monster_mechanics_find_family("revenants");
    int shamans = rc_monster_mechanics_find_family("lizardman_shamans");
    assert(dragonfire >= 0 && leafy >= 0 && finishers >= 0);
    assert(poison >= 0 && venom >= 0 && disease >= 0);
    assert(mirror >= 0 && spectre >= 0 && revenants >= 0 && shamans >= 0);
    assert(rc_monster_mechanics_find_family("missing_family") == -1);
    assert(!rc_monster_mechanics_family_has_npc(-1, 260));
    assert(rc_load_monster_mechanics(NULL) == -1);
    assert(rc_load_monster_mechanics("missing_regular_npc_mechanics.bin") == -1);

    assert(rc_monster_mechanics_family_has_npc(dragonfire, 260)); /* green dragon */
    assert(!rc_monster_mechanics_family_has_npc(dragonfire, 241)); /* baby blue dragon */
    assert(rc_monster_mechanics_family_has_npc(leafy, 410)); /* kurask */
    assert(rc_monster_mechanics_family_has_npc(leafy, 426)); /* turoth */
    assert(rc_monster_mechanics_family_has_npc(finishers, 412)); /* gargoyle */
    assert(rc_monster_mechanics_family_has_npc(poison, 406)); /* cave crawler */
    assert(rc_monster_mechanics_family_has_npc(venom, 8058)); /* Vorkath */
    assert(rc_monster_mechanics_family_has_npc(disease, 626)); /* Fever spider */
    assert(rc_monster_mechanics_family_has_npc(mirror, 417)); /* basilisk */
    assert(rc_monster_mechanics_family_has_npc(spectre, 2)); /* aberrant spectre */
    assert(rc_monster_mechanics_family_has_npc(revenants, 7940)); /* rev dragon */
    assert(rc_monster_mechanics_family_has_npc(shamans, 6766));

    uint64_t dragon_tags = rc_monster_mechanics_tags_for_npc(260);
    assert((dragon_tags & RC_MONSTER_TAG_DRAGONFIRE) != 0);
    assert((dragon_tags & RC_MONSTER_TAG_EQUIPMENT_MITIGATION) != 0);
    assert((dragon_tags & RC_MONSTER_TAG_POTION_MITIGATION) != 0);

    uint64_t turoth_tags = rc_monster_mechanics_tags_for_npc(426);
    assert((turoth_tags & RC_MONSTER_TAG_WEAPON_DAMAGE_GATE) != 0);
    assert((turoth_tags & RC_MONSTER_TAG_AMMO_DAMAGE_GATE) != 0);
    assert((turoth_tags & RC_MONSTER_TAG_SPELL_DAMAGE_GATE) != 0);

    uint64_t shaman_tags = rc_monster_mechanics_tags_for_npc(6766);
    assert((shaman_tags & RC_MONSTER_TAG_JUMP_ATTACK) != 0);
    assert((shaman_tags & RC_MONSTER_TAG_AREA_ATTACK) != 0);
    assert((shaman_tags & RC_MONSTER_TAG_MINION_SPAWN) != 0);
    uint64_t basilisk_tags = rc_monster_mechanics_tags_for_npc(417);
    assert((basilisk_tags & RC_MONSTER_TAG_MIRROR_SHIELD_REQUIRED) != 0);
    uint64_t spectre_tags = rc_monster_mechanics_tags_for_npc(2);
    assert((spectre_tags & RC_MONSTER_TAG_NOSE_PEG_REQUIRED) != 0);
    uint64_t vorkath_tags = rc_monster_mechanics_tags_for_npc(8058);
    assert((vorkath_tags & RC_MONSTER_TAG_VENOM) != 0);
    assert((vorkath_tags & RC_MONSTER_TAG_POISON) == 0);

    reset_mechanics();
    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.monster_mechanics_path = RC_TEST_REGULAR_NPC_MECH_BIN;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_monster_mechanic_family_count == 16);
    rc_world_destroy(world);

    printf("test_regular_npc_mechanics_bin: runtime family index loaded.\n");
    return 0;
}
