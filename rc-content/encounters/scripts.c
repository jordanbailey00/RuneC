#include "../content.h"
#include "../../rc-core/encounter.h"

static const char *SCRIPT_STUBS[] = {
    "duke_awakening_animation",
    "duke_sleeping_phase",
    "gauntlet_prep_phase",
    "huey_body_pieces_phase",
    "huey_head_phase",
    "hunllef_engage",
    "hydra_enrage_entry",
    "hydra_head_drop",
    "hydra_serpentine_entry",
    "inferno_triple_jad",
    "inferno_wave_progression",
    "inferno_zuk_phase",
    "kq_shed_exoskeleton",
    "kraken_whirlpool_phase",
    "leviathan_enter_enrage",
    "muspah_first_special",
    "muspah_form_swap",
    "muspah_second_special",
    "muspah_shield_up",
    "nex_blood_phase",
    "nex_ice_phase",
    "nex_shadow_phase",
    "nex_smoke_phase",
    "nex_zaros_phase",
    "nightmare_phase_1",
    "nightmare_phase_2",
    "nightmare_phase_3",
    "olm_phase_1",
    "olm_phase_2",
    "olm_phase_3",
    "scorpia_summon_guardians",
    "scurrius_center_rage",
    "scurrius_heal_at_food_pile",
    "sire_respiratory_phase",
    "sire_stun_on_wall",
    "sire_walking_phase",
    "tempoross_skilling_loop",
    "verzik_phase_1",
    "verzik_phase_2",
    "verzik_phase_3",
    "vetion_second_form_spawn",
    "wardens_phase_1",
    "wardens_phase_2",
    "wardens_phase_3",
    "whisperer_enter_shadow_realm",
    "wintertodt_skilling_loop",
    "yama_enrage",
    "zalcano_damage_phase",
    "zalcano_shielded_phase",
    "zulrah_drive_rotation",
};

void rc_content_encounter_script_stubs_register(struct RcWorld *world) {
    int count = (int)(sizeof(SCRIPT_STUBS) / sizeof(SCRIPT_STUBS[0]));
    for (int i = 0; i < count; i++) {
        if (!rc_encounter_script_lookup(world, SCRIPT_STUBS[i])) {
            rc_encounter_register_script(world, SCRIPT_STUBS[i],
                                         rc_encounter_script_noop);
        }
    }
}
