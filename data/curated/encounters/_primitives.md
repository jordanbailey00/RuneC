# Encounter primitives and field reference

This file is the reference catalog for the encounter authoring DSL
under `data/curated/encounters/`.

Use it to answer:
- what `primitive = "..."` names mean
- which mechanics are intended to be generic engine primitives
- which names are encounter-specific scripts instead
- what extra attack/phase/encounter-level fields mean in encounter TOMLs

Relationship to the codebase:
- generic primitives map to engine-owned implementations in
  `rc-core/encounter_prims.c`
- encounter-specific scripts are named via `script = "..."` and belong
  in `rc-content/encounters/<boss>.c`
- exporter/runtime mappings live in `rc-core/encounter.h` and
  `tools/export_encounters.py`

This file is an authoring/reference document, not a planning document.
Implementation sequencing belongs in `work.md`.

When adding a new encounter, check this list first. Reuse an existing
primitive when the semantics match. Add a new primitive row only when
the mechanic is truly generic and reusable.

---

## Mechanics primitives

| Primitive | Used by | Semantics |
|---|---|---|
| `telegraphed_aoe_tile` | Scurrius (Falling Bricks) | Flash shadow on target tile(s), deal damage N ticks later if player still standing there. Params: `target_current_tile`, `extra_random_tiles`, `warning_ticks`, `damage_min`, `damage_max`, `solo_damage_max`. |
| `spawn_npcs` | Scurrius (Minions) | Periodically spawn N NPCs of a given name/type, distributed around the arena. Params: `npc_name`, `count`, `distribution`, `persist_after_death`. |
| `spawn_npcs_once` | Scorpia (Summon Guardians) | One-shot variant: spawn on a trigger (not periodic). Params same as `spawn_npcs` minus period. Trigger via `trigger = "phase_enter:<id>"`. |
| `heal_at_object` | Scurrius (Food Heal) | NPC pathfinds to a named object (Food Pile), heals on a tick cadence up to a cap. Params: `object_name`, `heal_per_player`, `heal_ticks_cap`, `tick_period`. Scales by player count in multi. |
| `periodic_heal_boss` | Scorpia (Guardian Heal) | While one or more named NPCs are alive, the boss heals periodically. Params: `heal_per_tick`, `period_ticks`. Trigger via `trigger = "while_alive:<npc name>"`. |
| `drain_prayer_on_hit` | KQ (Barbed Spines) | When a specified attack lands for > 0 damage, drain N prayer points. Optionally mitigated by spectral shield. Bound to an attack via `bound_to`. Params: `points`, `spectral_shield_mitigation`. |
| `chain_magic_to_nearest_player` | KQ (Magic Bounce) | Multi-player only: magic attack bounces from primary target to nearest remaining player. Solo mode = no-op. Params: `max_bounces`. |
| `preserve_stat_drains_across_transition` | KQ (stat persistence) | Stat-drain effects (DDS, BGS) applied to one phase persist when the boss transitions to the next. Trigger via `trigger = "phase_exit:<id>"`. No params beyond that. |
| `teleport_on_incoming_attack` | Giant Mole (Burrow Escape) | On each incoming attack within an hp band, chance to teleport the boss to a random arena tile. Params: `hp_pct_band`, `chance_per_incoming`, `exclude_splashed_magic`, `exclude_poison_dot`, `distribution`, `extinguishes_light`, `drops_aggression`. |
| `teleport_player_nearby` | Chaos Elemental (Confusion) | Teleport the targeted player to a random tile within distance range. Params: `min_distance`, `max_distance`, `constrain_to_arena`. |
| `unequip_player_items` | Chaos Elemental (Madness) | Unequip up to N worn items from the targeted player. Params: `count`, `weapon_priority`, `affects_slots` (list). |
| `positional_aoe` | Corp Beast (Stomp) | Deal AoE damage on a period if a trigger condition is met (e.g. player is under boss). Params: `trigger_condition`, `damage_min`, `damage_max`, `prayer_ignorable`. |
| `spawn_leech_npc` | Corp Beast (Dark Energy Core) | Periodically spawn a helper NPC that leeches HP from adjacent players and heals the boss. Params: `npc_name`, `leech_per_tick`, `heals_boss_with_leeched`, `poison_weak`. |
| `regen_when_no_player` | Corp, Cerberus, Kraken | While the arena has no players, boss regenerates. Params: `heal_per_tick`, or `heal_to_full`, `untargetable_when_empty`, `reset_all_npcs`. |
| `attack_counter_special` | Cerberus (Triple Attack) | Fire a special attack on specific attack counts. Params: `first_attack_trigger`, `every_n_attacks`, `sequence` (list of {style, tick_delay, ignores_distance}), `max_hit`, `priority`. |
| `spawn_soul_attackers` | Cerberus (Summoned Souls) | Spawn N transient attackers each using a distinct style. HP threshold gates availability. Each prayer-blockable with prayer drain when blocked. Params: `every_n_attacks`, `min_hp`, `max_hp`, `styles`, `damage_if_unblocked`, `prayer_drain_if_blocked` (+ with_ward / with_shield / with_both), `skip_chance`, `priority`. |
| `dot_tile_placement` | Cerberus (Lava Pools) | Place DoT tiles (one on player + N random) that deal damage over time while stood on. Params: `every_n_attacks`, `max_hp`, `pool_count`, `pools_on_player`, `pools_random`, `dot_per_tick`, `dragonfire_protection_applies`, `skip_chance`, `priority`. |
| `static_dot_line` | Cerberus (Fire Line) | Place a static damage line at a fixed location, damaging on cross. Params: `damage_per_cross`, `trigger`. |
| `spawn_hidden_minions` | Kraken (Tentacles/Whirlpools) | Spawn N minion NPCs hidden behind an interactable object until the object is clicked. Params: `npc_name`, `count`, `hidden_until_attacked`, `hidden_object_name`, `tentacle_max_hit` (minion's own max hit). |
| `phase_advance_on_condition` | Kraken (Surface After Tentacles) | Advance to a target phase when a condition is met during the current phase. Params: `condition`, `advance_to`. |
| `group_kill_required` | Dagannoth Kings | Drops granted only when all named NPCs in a group die. Params: `required_npc_ids`, `grant_drops_for`. |
| `periodic_respawn_if_dead` | Dagannoth Kings, General Graardor | Dead NPC in a group respawns after a cooldown if other group members are still alive. Params: `respawn_cooldown_ticks`, `applies_to_npc_ids`. |
| `form_transition_dive` | Zulrah | Between rotation steps: dive animation, relocate tile, re-emerge as next form. Untargetable during dive; in-flight attacks still resolve. Params: `dive_ticks`, `resurface_ticks`, `untargetable_during_dive`, `in_flight_attacks_still_resolve`. |
| `attack_counter_alternate_special` | Vorkath | Every Nth attack fires a special; specials alternate strictly across a list. Params: `standard_attacks_between`, `specials` (list), `rotation` (strict_alternate / round_robin). |
| `covered_arena_environment` | Vorkath acid | Environment-wide effect for N ticks (arena floor covered in DoT; player must navigate safe path). Params: `duration_ticks`, `dot_per_tick`, `safe_path_navigation`. |
| `damage_reduction_until_mechanic_triggered` | Alchemical Hydra vents | Damage reduction in effect until a named event fires (e.g. correct vent sprayed). Params: `damage_reduction_pct`, `clear_on`, `increase_damage_on`, `increase_damage_pct`. |
| `animation_warning_style_swap` | Alchemical Hydra | Play a warning animation N attacks before switching combat style. Params: `every_n_attacks`, `warning_animation_ticks`. |
| `converging_aoe` | Alchemical Hydra phase 2 | N projectiles converge on player from multiple directions. Params: `bolt_count`, `converges_on_player`, `damage_per_bolt`. |
| `stun_then_fire_walls` | Alchemical Hydra phase 3 | Stun player then spawn fire walls blocking escape. Params: `stun_ticks`, `fire_wall_damage`. |
| `moving_dot_line` | Alchemical Hydra phase 3 | Flame trail that follows the player. Params: `trail_length`, `dot_per_tick`. |
| `object_interaction_ticked` | Alchemical Hydra vents | Player clicks a named object; after N activation ticks, a mechanic fires. Params: `objects` (list), `activation_ticks`. |
| `totem_charge_progression` | The Nightmare | N totems in arena charge via player attacks; all charged → boss takes damage + advance phase. Params: `totem_count`, `charge_per_attack`, `total_charge_per_totem`, `advance_damage_per_full_charge`, `trigger_advance`. |
| `telegraphed_portal_aoe` | Nightmare Grasping Claws | Black portal + random extra portals; damage if player stands on ANY portal. Params: `portal_count_extra`, `warning_ticks`, `damage_max`, `prayer_ignorable`. |
| `spawn_paired_husks` | Nightmare Husks | Spawn 2 paired NPCs around random player; paired NPCs use different styles; target player immobilized until both dead. Params: `pair_count`, `husk_blue_style`, `husk_green_style`, `target_random_player`, `immobilizes_target`. |
| `quadrant_safe_zone_dot` | Nightmare Corpse Flowers | Arena divided into quadrants; one safe, rest deal DoT with boss-heal multiplier. Params: `safe_quadrants`, `unsafe_dot_per_tick`, `heals_boss_multiplier`. |
| `shuffle_player_prayers` | Nightmare Curse | Remap player prayer click → activates different prayer. Lasts N attacks. Params: `duration_attacks`, `shuffle_mapping` (list of {from, to}). |
| `infectious_dot_with_cure` | Nightmare Parasites | Timed debuff on players; burst-damage if not cured; spawns NPC that attacks / reverses progression. Params: `incubation_ticks`, `burst_damage`, `burst_damage_if_cured`, `cure_items`, `parasite_hp_uncured`, `parasite_hp_cured`, `reverses_totem_charge`, `restores_boss_shield` (range). |
| `spawn_convergent_minions` | Nightmare Sleepwalkers | Spawn N minions that walk toward the boss; each absorbed increments a "charge" that fires as unavoidable AoE. Params: `npc_name`, `count_per_player`, `walks_toward`, `absorb_damage_per_walker`, `power_blast_on_absorption`, `power_blast_min`. |
| `aoe_tile_debuff` | Nightmare Spores | Tile-based AoE that applies a debuff (not damage). Spreads to adjacent players. Params: `pattern`, `aoe_size`, `debuff` (object), `debuff_ticks`, `spreads_to_adjacent_players`. |
| `line_dash` | Nightmare Surge | Boss teleports to edge then dashes to opposite edge, damaging everyone on the line. Params: `damage_max`, `direction`. |
| `spawn_objective_npcs` | Abyssal Sire (Respiratory Systems) | Spawn N NPCs at fixed arena slots; killing all advances to the named phase. Params: `npc_name`, `count`, `distribution`, `advance_phase_on`, `advance_to`. |
| `npc_pathed_movement` | Abyssal Sire (Walk to Wall) | Boss walks along a path to a destination; phase advances on arrival. No attacks during. Params: `destination`, `ticks_to_complete`, `advance_to`. |
| `spawn_wall_tentacles` | Abyssal Sire (Tentacles) | Spawn N tentacles at wall positions; all-dead advances phase. Params: `tentacle_count`, `tentacle_positions`, `advance_phase_on`, `advance_to`. |
| `teleporting_tile_aoe` | Phantom Muspah (Lightning Clouds) | Boss teleports around arena spawning AoE tile hazards while mechanic plays. Optionally boss remains targetable. Safe tile exists. Params: `cloud_count`, `damage_max`, `safe_tile_exists`, `boss_untargetable_during`, `damage_during_counts_toward_progression`, `boss_reappears_at`. |
| `homing_projectiles_with_walls` | Phantom Muspah (Homing Spikes) | Slow-moving projectiles that home in on player; block on idle blockers (previously-spawned spikes). Harden to static after N ticks. Params: `spike_count_range`, `tiles_per_tick`, `allows_diagonal`, `blocked_by_idle_spikes`, `harden_ticks`, `damage_on_hit`, `excludes_under_boss_tiles`. |
| `smite_drain_shield` | Phantom Muspah (Prayer Shield) | Boss raises prayer shield; player's Smite drains it per hit, with bonus drain from certain bolt specials. Shield-down → phase advance. Params: `shield_hp`, `hits_to_drain`, `sapphire_bolt_bonus_drain`. |
| `periodic_spike_cluster` | Phantom Muspah (Final Spike Barrage) | Every N attacks, spawn a cluster of spike tiles including one on player; grows from prior clusters. Params: `every_n_attacks`, `cluster_tiles`, `one_on_player`, `damage_on_hit`, `grows_from_existing_clusters`. |
| `moving_rotational_hazards` | Vardorvis (Spinning Axes) | N hazards orbit the boss in a circle; damage on contact. Params: `axe_count`, `orbit_radius`, `rotation_ticks_per_full_circle`, `damage_on_hit`. |
| `heal_boss_on_player_attack_miss` | Vardorvis (Mind Flay) | Telegraphed grab attack — if player attacks boss during telegraph, attack is cancelled AND boss heals. Params: `warning_ticks`, `heal_per_player_attack`, `cancel_player_attack`. |
| `tile_debuff_on_stand` | Vardorvis (Grey Tiles) | Tiles appear; stepping on one deals damage + applies debuff. Params: `tile_count`, `damage_on_step`, `debuff` (object). |
| `spawn_buff_zone_npc` | Leviathan (Abyssal Pathfinder) | Helper NPC with AoE buff zone: inside the zone player gets buffs (perfect accuracy, min-damage floor, style damage multiplier); outside gets debuffs (prayer-pierce, reduced damage). Params: `npc_name`, `spawn_priority_tiles`, `buff_aoe_size`, `buffs_inside` (object), `debuffs_outside` (object). |
| `one_shot_arena_effect` | Leviathan (Enrage Roar) | One-shot arena-wide effect fired at a trigger (not periodic). Params: `effect`, `tile_count`, `damage_max`. |
| `player_sanity_tracker` | The Whisperer | Track a per-player sanity meter that drains from certain events and regens from others; going insane ejects player. Params: `max_sanity`, `drain_per_*` events, `regen_per_*` events, `insane_threshold`, `insane_action`, `restored_on_boss_death`. |
| `spawn_tentacle_projectiles` | The Whisperer (Tentacle Drain) | Periodically spawn tentacle tiles that damage + drain sanity on contact. Params: `tentacle_count`, `warning_ticks`, `damage_on_hit`, `sanity_drain_on_hit`, `enrage_period_ticks`. |
| `aoe_prayer_swap_demand` | The Whisperer (Screech) | Telegraphed AoE that requires a specific prayer to be active on impact; wrong prayer deals damage + drains sanity. Params: `warning_ticks`, `required_prayer_cycle` (list), `wrong_prayer_damage`, `wrong_prayer_sanity_drain`. |
| `audio_visual_disruption` | The Whisperer (Silent Mode) | Temporarily hide audio cues + overhead warnings; player must rely on visual telegraphs only. Params: `duration_ticks`, `disables_audio_cues`, `hides_overhead_warning`. |
| `object_item_interaction` | Duke Sucellus (Herb Throwing) | Player clicks a named object while holding a specific item; N correct interactions advance phase; wrong interactions damage + delay. Params: `herb_piles` (list of {name, on_correct}), `wrong_herb_damage`, `wrong_herb_delays_wake_ticks`, `correct_herbs_to_wake`, `advance_to`. |
| `passive_heal_during_phase` | Duke Sucellus (Sleep Regen) | Slow passive boss regen during a specific phase; interrupted by a named event. Params: `heal_per_tick`, `cancelled_by_correct_herb`. |
| `attack_counter_style_swap` | Gauntlet Hunllef | Swap boss style every N attacks. Params: `every_n_attacks`, `cycle` (list of styles). |
| `spawn_tracking_tornadoes` | Gauntlet, Verzik P3 | Spawn N tornadoes that chase the player (optionally one per player); damage on contact. Params: `tornado_count`, `damage_on_contact`, `tornado_duration_ticks`, `player_specific`. |
| `forced_prayer_switch_on_style_swap` | Gauntlet | Bind to a style-swap mechanic: wrong prayer = full damage, correct = fully blocked. No parameters; derives from bound style-swap. |
| `hp_gated_style_increase` | Sol Heredit | Unlock new attacks in the boss rotation at each HP threshold. Params: `thresholds` (list of {hp_pct, unlock}). |
| `run_level_modifier_registry` | Colosseum, ToA Invocations | Player-selected modifiers that accumulate across a run and affect future mechanic behavior. Other mechanics query the registry. Params: `modifiers` (list of {id, effect}), or `invocations`. |
| `line_of_sight_shield` | TzKal-Zuk | Moving shield blocks boss attacks only when it's in line between boss and player. Failure → massive damage. Params: `shield_speed_tiles_per_tick`, `shield_path`, `blocks_zuk_attacks`, `failure_damage`. |
| `hp_gated_healer_spawn` | TzKal-Zuk | At named HP thresholds, spawn healer NPCs that heal the boss. Player can attack healers (attacks absorb into healer hp). Params: `spawn_thresholds` (list), `healer_npc`, `heal_per_tick`, `absorb_damage_when_player_attacks`. |
| `destructible_pillars` | TzKal-Zuk, Verzik P1 | Arena pillars with hp that degrade over time from boss attacks. Can be used as cover / line-of-sight breakers. Params: `pillar_count`, `pillar_hp`, `pillar_damaged_per_zuk_attack` / `damaged_per_electrify`. |
| `wave_end_minion` | TzKal-Zuk (Jal-Nib-Rek) | Single trivial-hp NPC spawns at encounter end as victory formality. Params: `npc`, `count`, `is_victory_lap`, `drops`. |
| `multi_limb_boss` | Great Olm | Boss composed of multiple NPC sub-entities (head + hands). Each has own HP + style + targetability. Head invulnerable until hands reduced. Params: `limbs` (list of {name, hp, default_targetable, attack_style}), `head_vulnerable_when`. |
| `player_position_swap` | Great Olm Phase 3 | Periodically teleport player to a random distant tile. Params: `min_distance`. |
| `environmental_wall_spawn` | Great Olm | Spawn a wall across arena that damages on cross. Params: `wall_length`, `damage_on_cross`. |
| `tile_telegraph_lightning` | Great Olm | Lightning tiles telegraph then damage. Params: `telegraph_ticks`, `damage_on_tile`, `tiles_per_volley`. |
| `continuous_heal_unless_interrupted` | Great Olm Final | Boss continuously heals unless player interrupts via named object interaction on cooldown. Params: `heal_per_tick`, `interrupt_object`, `interrupt_cooldown_ticks`. |
| `one_shot_weapon_provided` | Verzik Dawnbringer | Player receives a special weapon for one phase; removed at phase end. Params: `item`, `effective_max_hit`, `removed_at_phase`. |
| `spawn_web_tiles` | Verzik P2 | Web-tile spawner that immobilizes on contact. Params: `web_tile_count`, `immobilizes_player_ticks`. |
| `spawn_colored_nylocas` | Verzik P2 | Waves of color-coded minions; each color has a combat style; must be killed with matching style. Heals boss if left alive. Params: `nylo_colors` (list), `heals_verzik_if_not_killed`, `heal_per_nylo_per_tick`. |
| `persistent_dot_tile_pool` | Verzik P3 Yellows | DoT tiles that accumulate and persist until phase end. Params: `duration_ticks` (0 = persist), `tile_count_per_spawn`, `dot_per_tick`. |
| `obelisk_dps_check` | Wardens P1 | DPS check on a central obelisk within a time limit; fail = encounter fails. Params: `obelisk_hp`, `time_limit_ticks`. |
| `spawn_energized_pylons` | Wardens P3 | Pylons around arena; standing in one's field = damage buff. Destroyed pylons lose their buff. Params: `pylon_count`, `buff_range_tiles`, `buff_damage_multiplier`. |
| `periodic_death_tile_wave` | Wardens P3 | Waves of tiles spawn; contact = instakill. Safe-tile pattern alternates. Params: `wave_tile_count`, `safe_tile_pattern`, `damage_on_tile`. |

## Encounter-specific script primitives

These are bespoke to one encounter; the generic primitive list
doesn't cover them. Each needs a hand-authored C function keyed by
name (not a reusable primitive).

| Script | Used by | Purpose |
|---|---|---|
| `scurrius_heal_at_food_pile` | Scurrius phase 2 | Phase-entry logic: pick nearest Food Pile, walk to it, begin heal loop. |
| `scurrius_center_rage` | Scurrius phase 3 | Phase-entry logic: move boss to arena centre, switch attack speed, switch style distribution. |
| `scorpia_summon_guardians` | Scorpia phase 2 | Phase-entry trigger for one-shot guardian spawn (wraps `spawn_npcs_once`). |
| `kq_shed_exoskeleton` | Kalphite Queen transition | 20-tick untargetable transition animation between grounded and airborne forms. Legs-drop animation, spawn second-form body at end. |
| `kraken_whirlpool_phase` | Kraken opening phase | Phase-entry script: kraken dives, 4 whirlpool objects spawn; player must click each to reveal the Enormous Tentacle inside. Kraken untargetable until the surface condition fires. |

---

## Attack-level fields introduced

These are attack-object fields (not standalone primitives), but list
them here so engine code knows what to handle:

| Field | Used by | Semantics |
|---|---|---|
| `style = "random"` + `random_styles` + `style_weights` | Chaos Elemental Discord | Per-attack random style selection; damage rolls use the chosen style's stats + player defence against that style. |
| `forced_hit = true` | KQ Barbed Spines, KQ Magic Bounce | Attack always resolves as hit (may still roll 0 damage). Skips the accuracy roll. |
| `drains_prayer_on_damage` | KQ Barbed Spines | Drains N prayer when damage > 0. |
| `drains_prayer_if_praying` | Scorpia Melee | Drains prayer when a specified overhead prayer is active, regardless of whether damage landed. |
| `inflicts_poison = true` + `poison_starting_damage` | Scorpia | Apply poison DoT on hit, starting at a given tick damage. |
| `max_hit_solo` | Scurrius, KQ (for completeness) | Override max damage in solo mode (OSRS scales some bosses by player count). |
| `warning_ticks` | All | How many ticks before impact to show the telegraph / projectile. 0 = instant. |
| `accuracy_roll` | All melee | Which defence roll to use: `stab`, `slash`, `crush`. |
| `prayer_ignorable = true` | Corp Stomp, Kraken Magical Ranged | Overhead prayers have no effect on damage. |
| `style = "magical_ranged"` + `accuracy_using` + `defence_using` | Kraken | Hybrid-style: accuracy uses one bonus, defence uses another. Supports attacks that are "typeless" or cross-style. |
| `style = "random"` + `random_styles` + per-attack `style_weights` | Chaos Elemental Discord | Randomized style per projectile. |
| `style = "dragonfire"` / `"dragonfire_special"` | KBD | Custom styles that interact with antifire shields + potions per `[[damage_modifiers]]`. |
| `splits_into.count` / `per_proj_max_on` / `per_proj_max_adj` / `main_max_adj` | Corp Magic AoE Split | Attack fires a main projectile that splits into N sub-projectiles, each with its own max-hit on-tile and adjacent-to-tile damage values. |
| `on_hit.effect` / `effect_chance` / `effect_chance_protected` / effect-specific params (`poison_starting_damage`, `drain_amount`, `freeze_ticks`) | KBD special breaths, Corp drain-heal | On successful hit, apply a named effect with a given probability; protection stack can lower the probability (not just damage). |
| `on_hit.drain_one_of` + `drain_range` + `heal_boss_pct_of_damage` | Corp drain-heal magic | On hit, drain one of a set of player stats by a random amount in range; heal boss by a percentage of damage dealt. |
| `envenoms_on_hit = true` + `envenoms_starting_damage` | Zulrah, Alchemical Hydra | Venom applies on ANY hit even if prayer blocks damage. |
| `targets_tile_not_entity = true` | Zulrah Crimson, Vorkath Deadly Dragonfire | Attack targets a ground tile; damage applies only if player still on that tile at impact. |
| `fixed_damage_range` | Zulrah Crimson Tail Slam | Override the max_hit/RNG — roll uniformly in this fixed range. |
| `on_hit.stun_ticks` | Zulrah Crimson | Stun player for N ticks on hit. |
| `style = "alternating"` + `alternates` list | Zulrah Jad phase | Attack strictly alternates between styles across consecutive attacks. |
| `style = "melee_tile_slam"` / `"dragonfire_aoe"` / `"ranged_aoe_room"` | Various | Custom style enums for tile-targeted melee slams, dragonfire AoE attacks, room-wide ranged AoE. |
| `ignores_all_dragonfire_protection = true` | Vorkath Deadly | Dragonfire shield / antifire potion / Protect from Magic all do nothing. |
| `adjacent_tile_damage_scale` | Vorkath Deadly | If on-tile = full damage, adjacent-tile = this scale. |
| `requires_player_in_melee_range = true` | Graardor Shockwave | Attack only fires when current target is adjacent to the boss. |
| `hits_all_players_in_room = true` | Graardor Shockwave | Attack damages every player in the room, not just the current target. |
| `on_hit.effect_ignores_dragonfire_protection = true` | Vorkath Venomous / Corrupting | Dragonfire protection reduces damage but not the secondary effect (venom / prayer-off). |
| `min_hit` | Graardor Shockwave, Zilyana Magic, K'ril Magic/Special | Minimum damage value (damage uniformly rolled from min_hit to max_hit). |
| `conditional.used_when` | Kree'arra Talon Swipe | Attack only used when a named condition holds (e.g. `"no_player_targeting_boss"`). |
| `poison_pierces_prayer` | K'ril Melee | Poison inflict applies through Protect from Melee. |
| `on_hit.drain_prayer_pct` | K'ril Prayer-Pierce Special | Drain a percentage of the player's current prayer points on hit. |
| `aoe_shape` | Leviathan, Duke Sucellus | Shape of the damage area: `line_length_N`, `3x1_cross`, `3x3`, etc. |
| `on_hit.freeze_ticks` | Duke Sucellus Ice Beam | Freeze player for N ticks on hit. |
| `on_hit.drain_all_stats` | Duke Sucellus Lightning | Drain all combat stats by N on hit. |
| `requires_shield_alignment_fail` | Zuk Shield Blast | Attack triggers massive damage only if shield is NOT between boss and player. |
| `on_hit.splits_damage_across_players` | Verzik Purple Bomb | Damage rolled then split across all players in range. |
| `combo_sequence` | Sol Heredit Triple Strike | Multi-part attack where each sub-attack has own prayer/telegraph requirement. |
| `aoe_shape = "radial"` | Akkha Reprise | Radial AoE centered on boss. |
| `on_hit.inflict_venom` | Zebak Reprise | Inflict venom on successful hit. |

## Phase-level fields introduced

| Field | Used by | Semantics |
|---|---|---|
| `enter_at_hp_pct = N` | Scurrius, Obor, Bryophyta, Giant Mole, Chaos Elemental | HP% threshold to enter this phase. 100 = fight start. |
| `enter_at_hp = 0` | KQ transition | Hard HP value threshold (not %). |
| `enter_after = "<phase id>"` | KQ airborne | Enter this phase when another phase completes (no HP trigger). |
| `script = "<script name>"` | Scurrius heal/enraged, KQ transition | Phase-entry script (from encounter-specific list above). |
| `can_revert_to = "<phase id>"` | Scurrius enraged | If conditions return to a prior phase's trigger state, revert. |
| `allowed_attacks = [...]` | All | Whitelist of attack names usable in this phase. |
| `style_weights = { … }` | Scurrius phase 1, KQ | Weight distribution across attack styles. |
| `adjacency_style_weights` / `non_adjacent_style_weights` | KQ, Bryophyta | Style weights depending on whether player is adjacent. |
| `attack_speed_override` | Scurrius enraged | Per-phase attack speed override (base is in NDEF). |
| `overhead_prayers = [...]` | KQ | Boss-side active protection prayers (partial reduction, not immunity — see `[[protections]]`). |
| `untargetable = true` | KQ transition | Player can't target boss during this phase. |
| `duration_ticks = N` | KQ transition | Fixed-duration phase (when no HP trigger and no enter_after). |
| `enrage_timeout_ticks = N` + `enrage_action` | KQ airborne | Revert / transform if phase duration exceeds timeout. |
| `shield_present = true` + `damage_reduction_until_shield_broken` | Nightmare | Shield mechanic: scaled damage until shield HP depleted. |
| `enter_after = "totems_charged:N"` | Nightmare | Event-based phase entry (not HP-threshold). N totems charged → advance. |
| `required_vent` + `damage_reduction_until_vented` | Alchemical Hydra | Per-phase vent-spray requirement; 75% damage reduction until correct vent triggers. |
| `attack_pattern = "strict_alternate"` + `special_every_n_attacks` + `special_attack` | Hydra Enraged | Strictly alternate between two styles; fire a named special on every Nth attack. |
| `spawn_form_random` | Phantom Muspah opening phase | Randomly select boss form on fight start from a list (e.g. `["ranged","melee"]`). |
| `enter_after = "<phase>:if_spawn == 'X'"` | Phantom Muspah | Conditional phase entry keyed on encounter-level state (e.g. spawn form). |
| `attack_cycle_ticks` | Phantom Muspah | Fixed tick period between boss attacks (overrides base attack_speed for this phase). |
| `cycle_sequence` | Phantom Muspah shielded | Per-tick attack schedule within a cycle (list of {tick, attack}). Specifies exactly which attack lands on which tick of the cycle. |
| `shield_mechanic = "smite_drain"` | Phantom Muspah shielded | Phase uses the smite-shield primitive; player must use Smite to drain the shield before damage resolves. |
| `untargetable_reason` | Duke Sucellus sleep | Human-readable tag for why the boss can't be attacked this phase (for UI / log messages). |
| `on_enter.heals_boss` / `on_enter.restores_drained_stats` / `on_enter.teleports_player_to` | Whisperer enrage | Phase-entry side effects: flat heal, stat restoration, player relocation to a named arena. |
| `attack_pattern = "strict_alternate_pairs"` + `starts_with_style` | Whisperer enrage | Alternate pair-wise (2 attacks style A, 2 attacks style B, repeat); starts with a specified style. |

## Encounter-level fields introduced

| Field | Used by | Semantics |
|---|---|---|
| `on_death = { grant_drops_for_npc_id = N, skip_drops_for = [...] }` | KQ | Drop resolution for multi-id bosses: grant drops only on final-phase death, skip intermediate phases. |
| `[[bosses]]` array | Dagannoth Kings | Multi-boss encounter shape. Each entry has its own `npc_id`, `hp`, `attacks`, `damage_modifiers`. Shared `[[phases]]` apply to the whole group. |
| `[[damage_modifiers]]` with `applies_to_styles` | KBD antifire stack | Conditional damage scaling applied to incoming damage based on player state (shield, potion, buff). Stacks multiplicatively. |
| `[[damage_modifiers]]` with `condition` expression | Corp corpbane gate, DKS style lock, Zulrah form weakness, Vorkath halberd-only melee | Conditional damage scaling on fight-level based on the attacker's weapon / style / current boss form. Expressions like `weapon_not_corpbane_stab`, `incoming_style != magic`, `boss_form == 'green' AND incoming_style == 'ranged'`. |
| `[[damage_modifiers]]` with `replace_damage_range` | Zulrah damage cap | Replace incoming damage entirely with a roll from a given range when condition matches (e.g. damage > 50 → roll 45-50). |
| `[[rotations]]` array with `steps` list | Zulrah | Tick-driven rotation pattern: list of {form, arena_tile, duration_ticks}. Phase script drives stepping through rotation. |
| `[entry_requirement]` block | GWD bosses | Room-entry gates (killcount, quest stage, item). Not an in-fight mechanic; runtime enforces at chamber entry via varbit check. |
| `is_primary = true` / `is_bodyguard = true` flags on bosses | General Graardor, Zilyana, K'ril, Kree'arra | Classification within a multi-boss encounter for drop attribution and aggro targeting. |
| `applies_to_npc_ids` on `[[damage_modifiers]]` | Kree'arra (halberd-only melee) | Restrict a damage modifier to specific NPC ids within a multi-boss encounter. |
| `condition` string on `[[mechanics]]` | Phantom Muspah (conditional specials) | Additional guard on a mechanic firing — e.g. `"spawn_form == 'ranged' OR unused_at_75pct"`. |
| `[awakened_override]` block | Vardorvis, Duke Sucellus | Variant overlay: when the boss NPC id matches `applies_to_npc_id`, apply stat / mechanic multipliers (hp × 1.5, damage × 1.33, shorter mechanic periods). Avoids separate TOMLs per awakened variant. |
| `[corrupted_override]` block | Gauntlet (Corrupted Hunllef) | Variant overlay for alternate encounter mode with scaled stats. Same pattern as awakened_override. |
| `[[waves]]` array with `enemies` + `post_wave_modifier_choice` + `boss_fight` flags | Colosseum, Inferno | Multi-wave encounter: each wave spawns a specific enemy set; optional between-wave modifier choice; final wave flagged as boss fight. |
| `[[rooms]]` array with `sequence` + `path_boss` + `mechanics_summary` + `boss_npc` | CoX, ToB, ToA | Multi-room raid: rooms execute in order (ToB), random selection (CoX), or player-selected order (ToA path bosses). Each room has its own boss + mechanic list. |
| `starting_style` on phase | Gauntlet Hunllef | Override default style for a phase's first attack. |
| `wave_range` on phase | Inferno | Phase spans a wave-number range; intra-phase wave tracking separate from boss HP. |

## Protection-block extensions

| Field | Used by | Semantics |
|---|---|---|
| `[[protections]]` array on encounter | Obor (50% missile reduction), KQ (partial overhead prayers) | Per-attack + per-prayer damage scaling. Replaces the default full-block behavior. Params: `attack`, `player_prayer`, `damage_scale`. |

---

## Primitive-adoption checklist

When authoring a new encounter, for each mechanic:

1. Find the closest existing primitive in the table above.
2. If exact match: reuse. Add a row in your encounter's
   `[[mechanics]]` block referencing it.
3. If close but needs a new param: extend the primitive's param list
   here and reuse.
4. If no match: add a new primitive row with clear semantics. Keep
   primitives generic (accept params), not encounter-hardcoded, so
   future encounters can reuse them.
5. Update this file in the same commit as the encounter TOML.
