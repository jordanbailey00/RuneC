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
// Usage:
//
//   RcWorldConfig cfg = rc_preset_full_game();
//   RcWorld *w = rc_world_create_config(&cfg);
//   rc_content_register_all(w);     // full game
//
// or, for a custom bundle:
//
//   rc_content_scurrius_register(w);
//   rc_content_kalphite_queen_register(w);
//   // ...other modules you care about

struct RcWorld;

// Aggregate entry point — calls every content module register fn
// that's linked in. Isolated-sim builds link a subset of content
// .c files; the unused register fns simply aren't compiled in.
void rc_content_register_all(struct RcWorld *world);

// ---- Per-module register fns ------------------------------------------

// Encounters — boss-specific scripts (named by TOML `script = "..."`).
// Generic, shared mechanics (telegraphed_aoe_tile, spawn_npcs, etc.)
// live in rc-core/encounter_prims.c and are NOT per-boss.
void rc_content_scurrius_register(struct RcWorld *world);
void rc_content_kalphite_queen_register(struct RcWorld *world);
// Future entries land here as new boss modules are authored.

// Regions — region-specific NPC tick logic, object interactions, etc.
// (No region modules yet — Varrock data is loaded generically by
// rc-core/npc.c from region binaries.)

// Quests — per-quest varbit state machines.
// (No quest modules yet — pending quest subsystem maturity.)

#endif
