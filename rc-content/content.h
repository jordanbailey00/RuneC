#ifndef RC_CONTENT_H
#define RC_CONTENT_H

// rc-content — OSRS-specific game content (boss scripts, quest state
// machines, region-specific behavior). Depends on rc-core.
//
// See rc-content/README.md for the engine/content split rationale,
// per-module conventions, and how isolated-sim build targets select
// a content subset.
//
// Each content module lives in its own .c file under a category
// directory and exposes a single `rc_content_<name>_register` entry
// point called after rc_world_create_config(). The aggregate
// `rc_content_register_all` invokes every module whose .c file is
// linked into the binary; isolation targets just omit the .c files
// they don't want and the corresponding symbol is never referenced.
//
// Every register fn returns 1 on success and 0 on failure. Install the
// aggregate through rc_world_install_content so rc_world_reset re-runs it:
//
//   RcWorldConfig cfg = rc_preset_full_game();
//   RcWorld *w = rc_world_create_config(&cfg);
//   rc_world_install_content(w, rc_content_register_all);   // full game
//
// A custom bundle installs its own function calling the modules it needs.

struct RcWorld;
int rc_content_consumables_register(struct RcWorld *world);
int rc_content_storage_register(struct RcWorld *world);

// Aggregate entry point — calls every content module register fn
// that's linked in. Isolated-sim builds link a subset of content
// .c files; the unused register fns simply aren't compiled in.
int rc_content_register_all(struct RcWorld *world);

// ---- Per-module register fns ------------------------------------------

// Combat — OSRS-specific regular-NPC combat hooks. Generic combat state,
// formulas, hit queues, and attack timing stay in rc-core.
int rc_content_combat_register(struct RcWorld *world);

// Encounters — boss-specific content (named by TOML `script = "..."`, or
// option handlers such as Vorkath's Poke). Generic mechanics live in
// rc-core/encounter_prims.c and are NOT per-boss.
int rc_content_vorkath_register(struct RcWorld *world);
// Scripts for encounters that are not admitted (no support level); kept
// registered until those encounters are verified or removed.
int rc_content_alchemical_hydra_register(struct RcWorld *world);
int rc_content_the_nightmare_register(struct RcWorld *world);
int rc_content_phantom_muspah_register(struct RcWorld *world);
int rc_content_duke_sucellus_register(struct RcWorld *world);
int rc_content_leviathan_register(struct RcWorld *world);
int rc_content_whisperer_register(struct RcWorld *world);
int rc_content_gauntlet_register(struct RcWorld *world);
int rc_content_nex_register(struct RcWorld *world);
int rc_content_raids_register(struct RcWorld *world);
// Future entries land here as new boss modules are authored.

// Regions — region-specific NPC tick logic, object interactions, etc.
// (No region modules yet — Varrock data is loaded generically by
// rc-core/npc.c from region binaries.)

// Quests — per-quest varbit state machines.
// (No quest modules yet — pending quest subsystem maturity.)

#endif
