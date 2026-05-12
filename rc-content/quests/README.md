# rc-content/quests — per-quest state machines

This directory holds quest-specific state machines and helper code.

Use this file for:
- what quest modules are for
- how quest code relates to quest data
- how files in this directory should be organized

Quest data and quest code are separate:
- `data/curated/quests/`
  - human-authored quest structure and step data
- `data/defs/quests.bin`
  - compact quest metadata used by runtime systems
- `rc-content/quests/`
  - code that reacts to interactions, dialogue, varbits, and quest
    progression rules that cannot live as pure data

## Module pattern

```c
// rc-content/quests/cooks_assistant.c
#include "../content.h"

// Varbit references — from data/defs/varbits.bin index map.
#define VAR_COOKS_ASSISTANT_STAGE  1748

static void on_talk_to_cook(struct RcWorld *w, ...) {
    // Advance the quest stage varbit based on current state + dialogue
    // choice. Fire RC_EVT_QUEST_STAGE_CHANGED.
}

void rc_content_cooks_assistant_register(struct RcWorld *world) {
    rc_event_subscribe(world, RC_EVT_DIALOGUE_CHOICE, on_talk_to_cook, ...);
}
```

## File structure

One `.c` file per quest, slug-named to match
`data/curated/quests/<slug>/steps.toml`:

```
quests/
├── cooks_assistant.c          → data/curated/quests/cooks_assistant/steps.toml
├── sheep_shearer.c
├── romeo_and_juliet.c
├── ...
└── dragon_slayer_2/            (multi-file for long quests)
    ├── dragon_slayer_2.c       (entry + register)
    ├── quest_steps/
    └── ...
```

## When code belongs here

Quest code belongs here when progression depends on:
- NPC interaction handlers
- object interaction handlers
- varbit/state transitions
- quest-specific scripted behavior

Pure quest data does not belong here.

## Reference sources

Per `rc-content/README.md`:
- `rsmod` is the modern OSRS code reference where quest coverage exists
- `void_rsps` and `2011Scape-game` are overlap-only references for
  older shared quests
- newer OSRS quest lines still require wiki reconstruction

This directory may be empty at times. Its role stays the same: it is
the home for quest-specific runtime code, not a planning placeholder.
