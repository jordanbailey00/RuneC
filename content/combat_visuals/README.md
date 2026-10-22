# Combat Visual Content

`visuals.tsv` is the RuneC-owned source table for combat presentation rows.
`tools/export_combat_visuals.py` validates this table and copies it to the
runtime TSV under `data/defs/`.

Do not regenerate this table from a cloned RSMod, RuneLite, private-server, or
wrong-game repository. If a row cannot be backed by b237 cache evidence or
reviewed RuneC-owned content, keep the missing fact as a source-gap row instead
of adding an external repo checkout as an exporter input.

`combat_visuals` is no longer a release-blocking source gap. NPC rows that use
the `curated` authority must remain reviewed RuneC-owned rows and carry a
`curated:b237:` note tag so the table keeps a concrete provenance trail.

Encounter rows are `npc` rows keyed by the NPC definition of the form that
records the action and by `attack_key`: the attack name, a mechanic part
(`<mechanic>` or `<mechanic>:<part>`) or a form change (`form:leave`,
`form:enter`), exactly as `docs/encounter_primitives.md` lists them. An
explicit row with primitive `none` documents an action that has no visual.
`tests/test_encounter_visuals.c` fails when an admitted encounter can record
a key without a row, names a missing spotanim, or uses a sequence outside
`all.anims` or another skeleton. Notes marked `accepted` (`timing=`, `arc=`,
`profile=`) carry values without a source that the user signed off on
2026-10-21; replace them when a source turns up.

Projectile origins use the client's parameters: an NPC projectile starts at
the NPC's centre, `proj_progress` fine units toward the target (128 per tile)
and `proj_start_height` above the ground. Measure both on the form's B237
model posed with the row's sequence at its `proj_delay` (the emitter's
forward distance on the centre line and its height); a travel model built
above its own origin starts that much lower. Each boss row's note records the
pose it was measured on. `test_encounter_visuals` fails when a projectile's
visible start lies more than half a tile from the posed body.
After editing rows, re-run the combat-visual and animation exports so the
sequences they name are in the animation packs.
