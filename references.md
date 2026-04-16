# Reference Repo Comparison

Deep comparison of the three reference codebases for porting OSRS to C. Each has different strengths. None is complete on its own. This document identifies exactly what to pull from where.

**Key constraint:** We are building a **single-player, local-only** game. All three reference repos are designed for multiplayer RSPS (RuneScape Private Server) with networking, player management, and login systems. We discard all of that. We only care about game logic, formulas, and data.

---

## Overview

| | RuneLite | RSMod | Void RSPS |
|--|---------|-------|-----------|
| **Language** | Java | Kotlin | Kotlin |
| **What it is** | OSRS client + plugins | OSRS server emulator | OSRS server emulator |
| **OSRS revision** | Current live (b237) | Current OSRS | ~2011 era (rev 718) |
| **Strength** | Cache format, data structures, IDs | Tick order, pathfinding, combat accuracy | Skills, content, Varrock data |
| **Weakness** | No game logic (it's a client) | Missing most skills, no quests | Slightly older formulas, heavy scripting |
| **Multiplayer overhead** | N/A (client) | ~3% (mostly in `api/net/`) | Moderate (login, sessions) |
| **Game logic extractable?** | Data/format only | Yes, cleanly separated | Yes, but wrapped in coroutines |

---

## RuneLite

**Path:** `/home/joe/projects/runescape-rl-reference/runelite/`

### What it is

RuneLite is the actual OSRS client with a plugin framework. It doesn't implement game logic — Jagex's servers do that. What RuneLite gives us is the **authoritative source for OSRS data formats** since it decodes the real OSRS cache.

### What we use it for

**Cache definitions (critical).** These are the real OSRS data structures decoded opcode-by-opcode from the b237 cache. This is where we learn exactly what fields exist on items, NPCs, objects, animations, and maps.

- `NpcDefinition.java` — name, size, combat level, stats[6] (atk/def/str/hp/rng/mag), all animation IDs (idle/walk/run/attack/death), model IDs, menu options, recoloring
- `ItemDefinition.java` — name, cost, stackable, members, noted variants, equipment positions (wearPos1/2/3), model IDs, recoloring, inventory/ground options
- `ObjectDefinition.java` — name, sizeX/Y, wallOrDoor flag, animationID, blockingMask, interactType, models, 5 menu options
- `SequenceDefinition.java` — frameIDs (packed: top 16 bits = FrameDefinition, bottom = frame index), frameLengths, leftHandItem/rightHandItem, frame sounds
- `ModelDefinition.java` — vertexCount, vertexX/Y/Z[], faceCount, faceIndices1/2/3[], faceColors[], faceTransparencies[], faceTextures[], UV coordinates, vertex groups for animation
- `MapDefinition.java` — 64x64x4 tile grid, per-tile: height, underlayId, overlayId, overlayShape, overlayRotation, settings flags
- `SpriteDefinition.java` — width, height, pixels[] (ARGB), palette-indexed format

**Loader opcodes.** The `*Loader.java` files document every cache opcode — which byte means what. Essential for building our cache decoder in `rc-cache/`.

**Collision flags.** `CollisionDataFlag.java` gives us the exact bit values:
```
BLOCK_MOVEMENT_NORTH = 0x2, EAST = 0x8, SOUTH = 0x20, WEST = 0x80
BLOCK_MOVEMENT_OBJECT = 0x100
BLOCK_MOVEMENT_FLOOR = 0x200000
BLOCK_LINE_OF_SIGHT_FULL = 0x20000
```

**ID constants.** `ItemID.java` (16,210 entries), `NpcID.java` (13,058 entries), `AnimationID.java` (392 named constants). Useful for looking up specific items/NPCs by name.

**XP table and combat level formula.** `Experience.java` has the exact formulas:
- XP for level N: `sum(floor(x + 300 * 2^(x/7))) / 4` for x=1..N-1
- Combat level: `base + max(melee, ranged, magic)` where base = `(def + hp + floor(prayer/2)) / 4`

**Coordinate system.** WorldPoint (absolute x/y/plane), LocalPoint (scene-relative, 128 units per tile), Constants (REGION_SIZE=64, SCENE_SIZE=104, CHUNK_SIZE=8, MAX_Z=4).

### What we DON'T get from RuneLite

- No combat formulas (damage rolls, accuracy, max hit) — that's server-side
- No skill training logic (mining success rates, cooking burn chances) — server-side
- No NPC AI — server-side
- No item equipment bonuses (attack/defence stats) — not in the cache, stored server-side
- No quest logic
- No shop logic
- No pathfinding algorithm

### Key files

```
cache/definitions/NpcDefinition.java        — NPC cache structure
cache/definitions/ItemDefinition.java       — Item cache structure
cache/definitions/ObjectDefinition.java     — Object cache structure
cache/definitions/SequenceDefinition.java   — Animation structure
cache/definitions/ModelDefinition.java      — 3D model structure
cache/definitions/MapDefinition.java        — Region tile data
cache/definitions/SpriteDefinition.java     — Sprite/icon format
cache/definitions/loaders/ItemLoader.java   — Item opcode decoder
cache/definitions/loaders/ObjectLoader.java — Object opcode decoder
runelite-api/api/CollisionDataFlag.java     — Collision bit flags
runelite-api/api/Experience.java            — XP table, combat level
runelite-api/api/coords/WorldPoint.java     — Coordinate system
runelite-api/api/ItemID.java                — All item IDs
runelite-api/api/NpcID.java                 — All NPC IDs
runelite-api/api/AnimationID.java           — Named animation IDs
```

---

## RSMod

**Path:** `/home/joe/projects/runescape-rl-reference/rsmod/`

### What it is

RSMod is an OSRS server emulator. 1,801 Kotlin files across 129 Gradle modules. It implements the actual game engine — tick processing, pathfinding, combat — but has limited content (only Lumbridge, no quests, few skills).

### What we use it for

**Tick processing order (critical).** `GameCycle.kt` documents the exact order things happen each 600ms tick. This is the single most important reference for getting game feel right:

```
1. World queues and delayed actions
2. NPC pre-tick
3. Player ID shuffle (randomize processing order)
4. Player input processing (client packets)
5. Player route requests
6. NPC main processing (reveal, hunt, regen, AI timers, queues, modes, interaction)
7. Controller processing
8. Player main processing (queues, timers, areas, engine queues, interaction)
9. Player logout
10. Player login
11. Map clock increment
12. Post-tick (world + player)
```

We simplify this heavily for single-player (no login/logout, no player shuffle, no controllers), but the NPC-before-player and the phase ordering matters.

**Pathfinding (critical).** `RouteFinding.kt` is a production-quality BFS implementation:
- 128x128 search map, 4096 waypoint ring buffer
- Optimized variants for 1x1, 2x2, and large entities
- Directional collision flag checking for all 8 directions
- Reach strategy system for approaching objects (handles doors, diagonal walls)
- Alternative routing when destination is unreachable
- Thread-safe (ThreadLocal buffers, no static arrays — avoids the FC segfault bug)

This is the best pathfinding reference. Better than FC's (which used uint8 walkability) and more complete than Void's.

**Combat accuracy formulas (critical).** Complete PvN (Player vs NPC) implementations:
- `PvNMeleeAccuracy.kt` — effective attack level with prayer/style/void, attack roll = eff_level * (bonus + 64), same for defence
- `PlayerRangedAccuracy.kt` — ranged accuracy with prayer modifiers
- `PlayerMagicAccuracy.kt` — magic accuracy
- `MaxHitFormulae.kt` — melee/ranged/magic max hit with all modifiers
- Special attack multipliers
- Spellbook variants (Ancient, Modern, Lunar)

These are separated into PvP and PvN. We only need PvN since there's no other player.

**Collision flag system.** `CollisionFlag.kt` defines all flags with directional variants for walls, objects, ground decor, projectile blocking, NPC/player blocking, and route blocking. More complete than RuneLite's flags.

**Shop system.** Complete buy/sell logic with stock restocking, currency handling, price calculation. Clean architecture.

**Dialogue system.** Full framework with `chatPlayer`, `chatNpc`, `choice2`-`choice5`, `mesbox`, `objbox`, expression animations. Uses Kotlin coroutines for async player input.

**Door/ladder mechanics.** Working door toggle with collision flag updates, double door coordination, ladder up/down with plane transitions.

**Equipment bonuses.** `WornBonuses.kt` — computes attack/defence stats from all equipped items. Covers stab/slash/crush/ranged/magic for both attack and defence, plus strength, ranged strength, magic damage, and prayer bonus.

### What RSMod is missing

- **Skills:** Only woodcutting is implemented. Mining, smithing, cooking, fishing, prayer, firemaking — all stubs (empty Gradle modules with no code).
- **Quests:** None. No quest framework at all.
- **NPC AI:** Framework exists (hunt system, mode processor) but actual behaviors aren't scripted. No wander, no aggression radius, no retreat, no patrol. The pieces are there but nothing puts them together.
- **Varrock content:** None. Only Lumbridge has spawns/NPCs.
- **Prayer effects:** The UI/interface exists but prayer bonuses aren't hooked into combat formulas.
- **Drop tables:** No loot generation system.

### Multiplayer overhead

Very low (~3% of codebase in `api/net/`). Game logic is cleanly separated. The pathfinder, combat formulas, and collision system have zero network coupling.

### Key files

```
api/game-process/GameCycle.kt                  — Tick processing order
engine/routefinder/RouteFinding.kt             — BFS pathfinding
engine/routefinder/flag/CollisionFlag.kt       — All collision flags
api/combat/combat-formulas/MaxHitFormulae.kt   — Max hit calculations
api/combat/combat-formulas/accuracy/melee/PvNMeleeAccuracy.kt
api/combat-accuracy/player/PlayerMeleeAccuracy.kt
api/combat-accuracy/player/PlayerRangedAccuracy.kt
api/combat-accuracy/player/PlayerMagicAccuracy.kt
api/player/bonus/WornBonuses.kt                — Equipment stat sums
api/shops/ShopScript.kt                        — Shop buy/sell
api/player/dialogue/Dialogue.kt                — Dialogue system
content/generic/generic-locs/doors/DoorScript.kt
content/generic/generic-locs/ladders/LadderScript.kt
content/skills/woodcutting/Woodcutting.kt      — Only working skill
engine/game/entity/Player.kt                   — Player state
engine/game/entity/Npc.kt                      — NPC state
engine/game/MapClock.kt                        — Global tick counter
```

---

## Void RSPS

**Path:** `/home/joe/projects/runescape-rl-reference/void_rsps/`

### What it is

A Kotlin OSRS private server emulator based on ~2011 RuneScape (rev 718). It has the most complete game content of the three — working skills, quests, shops, and crucially, complete Varrock data. Uses TOML files for content definitions.

### What we use it for

**Skills (critical).** Void is the **only reference with working skill implementations**. All seven skills we need for Varrock are complete:

- **Mining** (`content/skill/mining/Mining.kt`, 215 lines) — rock types, pickaxe tiers, dragon pickaxe special, rock depletion/respawn, Varrock Armour bonus, gem rock support
- **Smithing** (`content/skill/smithing/Anvil.kt`, 200+ lines) — 30+ item types (daggers, swords, armor, bolts), level requirements, bar consumption, cannonballs
- **Cooking** (`content/skill/cooking/Cooking.kt`, 140+ lines) — burn chance by level, cook/burn result, range-only items, quantities (1/5/X/All)
- **Fishing** (`content/skill/fishing/Fishing.kt`, 125+ lines) — fishing spots, tool/bait requirements, multiple fish per spot, spot movement detection
- **Woodcutting** — tree types, axe selection, depletion, XP
- **Prayer** (`content/skill/prayer/Prayer.kt`, 100+ lines) — bone burying, drain system, prayer bonuses from equipment, Turmoil stat drain
- **Firemaking** (`content/skill/firemaking/Firemaking.kt`, 150+ lines) — fire creation, log types, burn chance, tinderbox vs firelighters

The skill code is clean and follows consistent patterns. Success rate formulas, XP rewards, resource depletion — all there.

**Combat formulas.** Also complete, with a slightly different structure:
- `Hit.kt` (196 lines) — accuracy formula: offensive/defensive ratings, hit chance, prayer modifiers, slayer modifiers, Void set bonus (+10% melee, +45% magic), stance bonuses
- `Damage.kt` (180 lines) — max hit: `5 + (effective_level * strength_bonus) / 64`, prayer multipliers, special attack multipliers
- `Bonus.kt` — stance bonuses, Salve amulet, Slayer helmet
- Protection prayers: 40% reduction for players, 100% block for NPCs

These formulas are OSRS-accurate for the 2011 era. The core math (accuracy rolls, max hit) is the same as modern OSRS — the changes are in item-specific modifiers that didn't exist in 2011.

**Varrock content (critical).** This is the only repo with actual Varrock data:

- `data/area/misthalin/varrock/varrock.npc-spawns.toml` — 100+ NPC spawn entries with exact coordinates
- `data/area/misthalin/varrock/varrock.shops.toml` — 16+ shops fully stocked (general store, Horvik's armour, Aubury's runes, Zaff's staves, Lowe's archery, Thessalia's clothes, sword shop)
- `data/area/misthalin/varrock/varrock.npcs.toml` — 50+ NPC definitions with examine text, wander ranges
- `data/area/misthalin/varrock/varrock.objs.toml` — all interactive objects (doors, stairs, gates, anvils, furnaces, ranges)
- `data/area/misthalin/varrock/varrock.item-spawns.toml` — ground item spawns
- `data/area/misthalin/varrock/sewer/` — complete sewer content (separate NPC spawns, objects, teleports)

This TOML data is gold. We can parse it directly to get coordinates, shop inventories, and spawn data instead of guessing.

**Object interactions.** The best implementation of doors, gates, and stairs:
- `Door.kt` (265 lines) — single/double doors, hinge calculation, collision flag toggle, auto-reset timer, sound effects
- `DoubleDoor.kt` — coordinated double door opening
- `Gate.kt` — gate mechanics
- `Stairs.kt` — climb up/down with plane transitions
- `TrapDoors.kt` — trap door mechanics
- `Picking.kt` — herb/berry bush picking

**Quests.** Has several implemented, including:
- Demon Slayer (140+ lines) — retrieve 3 keys, defeat demon, Varrock-based
- Cook's Assistant, Doric's Quest, Imp Catcher, Prince Ali Rescue, Rune Mysteries, The Knight's Sword, The Restless Ghost, Gunnar's Ground
- Romeo & Juliet is NOT implemented (not found in free quests)

**Items/Equipment.** Complete equipment system: 
- All offensive/defensive bonuses (stab/slash/crush/ranged/magic for attack and defence)
- Prayer bonus, strength bonus, ranged strength, magic damage
- Void Knight set detection and effects
- Barrows set effects (Verac's armor bypass)
- Special attack multipliers per weapon type

**Game loop.** Clean 600ms tick loop in `GameLoop.kt` (60 lines). Simple sequential processing with performance monitoring. Easy to understand.

### What Void RSPS is missing

- **No modern OSRS content.** Based on 2011, so some items/NPCs post-2011 won't exist. Combat formulas are ~95% the same for what we need.
- **NPC AI is basic.** Wander (radius-based random walk), retaliate, retreat if too far from spawn. No complex behavior trees.
- **Dialogue uses coroutines.** The content is there but extracting dialogue trees requires unwinding Kotlin suspend functions into state machines.
- **Networking is baked in deeper** than RSMod. Not a problem since we only extract logic, not infrastructure.

### Multiplayer overhead

Moderate. Player arrays, session management, and packet encoding are throughout. But the game logic (formulas, skills, shops) is cleanly callable and doesn't depend on multiplayer state.

### Key files

```
game/src/main/kotlin/content/entity/combat/hit/Hit.kt      — Accuracy formula
game/src/main/kotlin/content/entity/combat/hit/Damage.kt    — Max hit formula
game/src/main/kotlin/content/entity/combat/Bonus.kt         — Stance/slayer modifiers
game/src/main/kotlin/content/skill/mining/Mining.kt          — Mining skill
game/src/main/kotlin/content/skill/smithing/Anvil.kt         — Smithing skill
game/src/main/kotlin/content/skill/cooking/Cooking.kt        — Cooking skill
game/src/main/kotlin/content/skill/fishing/Fishing.kt        — Fishing skill
game/src/main/kotlin/content/skill/firemaking/Firemaking.kt  — Firemaking skill
game/src/main/kotlin/content/skill/prayer/Prayer.kt          — Prayer skill
game/src/main/kotlin/content/entity/obj/door/Door.kt         — Door mechanics
game/src/main/kotlin/content/entity/obj/Stairs.kt            — Stair mechanics
game/src/main/kotlin/content/quest/free/demon_slayer/DemonSlayer.kt
data/area/misthalin/varrock/varrock.npc-spawns.toml          — Varrock NPC spawns
data/area/misthalin/varrock/varrock.shops.toml               — Varrock shops
data/area/misthalin/varrock/varrock.npcs.toml                — Varrock NPC defs
data/area/misthalin/varrock/varrock.objs.toml                — Varrock objects
engine/src/main/kotlin/world/gregs/voidps/engine/GameLoop.kt — Game tick loop
```

---

## Side-by-Side: What to use where

| System | Use this repo | Why not the others |
|--------|--------------|-------------------|
| **Cache decoder** | RuneLite | Only source with opcode-level format docs |
| **NPC/Item/Object IDs** | RuneLite | Has all 16k items, 13k NPCs by name |
| **Collision flags** | RuneLite + RSMod | RuneLite has exact bit values, RSMod has usage patterns |
| **XP table** | RuneLite | Exact formula in `Experience.java` |
| **Tick order** | RSMod | Most detailed, correctly ordered phases |
| **Pathfinding** | RSMod | Production BFS, thread-safe, multi-size entity support |
| **Combat accuracy** | RSMod | Clean PvN separation, modern formulas |
| **Combat max hit** | RSMod + Void | RSMod for structure, Void for readable formula |
| **Equipment bonuses** | RSMod | `WornBonuses.kt` sums all slots cleanly |
| **Mining** | Void | Only implementation. Rock types, depletion, XP |
| **Smithing** | Void | Only implementation. Item types, bar requirements |
| **Cooking** | Void | Only implementation. Burn chance, cook/fail logic |
| **Fishing** | Void | Only implementation. Spots, tools, catch tables |
| **Woodcutting** | Void (or RSMod) | Both implement it, Void is more complete |
| **Prayer** | FC + Void | FC has proven drain math, Void has buff effects |
| **Firemaking** | Void | Only implementation |
| **Shops** | RSMod + Void data | RSMod for buy/sell logic, Void for Varrock shop inventories |
| **Dialogue** | RSMod | Cleaner async model than Void's coroutines |
| **Doors** | Void | Best implementation (collision toggle, double doors, hinges) |
| **Stairs/ladders** | RSMod + Void | Both work, RSMod is simpler |
| **Quests** | Void | Only repo with any (Demon Slayer, etc.) |
| **Varrock NPCs** | Void | 100+ spawn entries with exact coordinates |
| **Varrock shops** | Void | 16+ shops with full stock lists |
| **Varrock objects** | Void | All interactive objects with positions |
| **NPC AI** | RSMod framework + Void behaviors | RSMod has hunt/mode system, Void has wander/retaliate |

---

## What none of them have

These things we'll need to build ourselves or derive from the OSRS wiki:

- **Item equipment bonuses** — Not in the cache. RuneLite doesn't expose them. Void has them hardcoded for 2011 items. For modern items, we'd need to scrape the wiki or manually enter them. For Varrock-scope items (bronze through rune gear, basic bows, staves) Void's data should cover it.
- **Drop tables** — What items NPCs drop on death. Not in any reference in a usable form. We'll need to define these per-NPC based on wiki data.
- **NPC aggression behavior** — The rules (aggro range, multi-combat zones, level-based aggression) are documented on the wiki but not fully implemented in any reference.
- **Romeo & Juliet quest** — Not in any reference. Demon Slayer is in Void. We'll script R&J ourselves based on wiki walkthrough.
- **Precise skill success rates** — Mining/cooking/fishing success chances at each level. Void has the structure but some formulas are approximated. Wiki has community-derived rates.

---

## Simplification opportunities

Because we're single-player, we can strip significant complexity from the reference code:

| Reference concept | Their version | Our version |
|------------------|--------------|-------------|
| Player arrays | `Player[2048]`, ID shuffling | Single `RcPlayer` struct |
| Network protocol | Packet encoding/decoding | Direct function calls |
| Login/logout | Session management, authentication | No login, world always running |
| Player interaction | Trading, following, PvP | None — only PvE |
| Chat system | Public/private/clan chat | Game messages only |
| World instances | Multiple WorldViews, instancing | One world, one plane active |
| Event bus | EventBus pub/sub with subscribers | Direct function calls in tick loop |
| Coroutines | Kotlin suspend for dialogue/skills | C state machine with switch |
| Dependency injection | Guice (RSMod) / Koin (Void) | None — pass struct pointers |
| Plugin system | Classpath scanning, module loading | Compiled-in content |

The reference codebases are 100k+ lines each because of infrastructure. Our game logic will be a fraction of that — the same formulas in straight C without the framework overhead.
