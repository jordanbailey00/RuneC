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
