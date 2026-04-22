# RuneC v1 Exclusions

This file lists systems that are explicitly out of scope for the first
playable / RL-baseline version of RuneC.

Use this file to answer:
- what we are not implementing for v1
- what data we should not scrape or model for v1
- what runtime systems should not shape current architecture

Do not use this file for planning. Planning lives in `work.md`.
If a system is in-scope later but not required for v1, it may still
appear here until the scope changes.

---

## 1. Multiplayer economy and player trading

Not implementing for v1:
- Grand Exchange offers, slots, taxes, item sinks, buy limits, price
  history, or live price APIs
- player-to-player trade, trade confirmation flows, or wealth checks
- any other shared-economy or cross-player market logic

Reason:
- RuneC v1 is single-player. It has no shared economy.

---

## 2. Multiplayer social, group, and account services

Not implementing for v1:
- friends list, ignore list, and private messaging
- friends chat / chat-channel, clan systems, clan halls, clan coffers,
  clan recruitment, and clan ranks
- Group Ironman
- world select, world hopping, hiscores, poll booths, and in-game polls
- account login, account authentication, session queues, 2FA, and
  display-name services
- bond pouch, bond redemption, and membership/account-management flows

Reason:
- these are network/account features, not local simulation features

---

## 3. Player-vs-player systems

Not implementing for v1:
- player-vs-player combat rules
- PvP worlds
- skull system, Bounty Hunter systems, PvP loot-to-killer timers, and
  anti-PK-specific rule surface
- PvP-first minigames and arenas:
  - Castle Wars
  - Clan Wars
  - Last Man Standing
  - Emir's Arena
  - Bounty Hunter
  - TzHaar Fight Pits
  - Soul Wars

Reason:
- RuneC v1 is PvE/single-player

Note:
- Wilderness regions, Wilderness bosses, and other Wilderness PvM
  content can still be implemented as PvM spaces without player-vs-
  player rules.

---

## 4. Meta trackers and completion overlays

Not implementing for v1:
- Achievement Diaries
- Combat Achievements
- Collection Log
- temporary or seasonal meta systems such as Leagues, Deadman, official
  speedrunning modes, and tournament ladders

Reason:
- these sit on top of gameplay rather than enabling gameplay

---

## 5. Random, seasonal, cosmetic, and social-expression systems

Not implementing for v1:
- random events
- holiday events and their unlock chains
- emote tab and emote unlock tracking
- mid-game appearance salons such as the barber, Thessalia, and the
  Makeover Mage
- pet insurance and reclaim loops

Reason:
- low value for the first playable / RL baseline relative to the extra
  data and runtime surface

---

## 6. Explicitly deferred from v1

Not implementing for v1:
- Treasure Trails / clue scroll system
- follower pets and menagerie storage
- Sailing

Reason:
- these are real OSRS systems, but they are large scope for low value
  to the initial playable baseline

Note:
- current live OSRS has 24 skills. Sailing was released on
  November 19, 2025 and is intentionally deferred from RuneC v1.

---

## 7. Guardrails

- Do not add schemas, exporters, curated templates, or runtime systems
  for excluded categories unless the scope changes first.
- Excluding a gameplay system from v1 does not exclude its items from
  the item database. Quest/minigame/deferred-system items should still
  be present as item definitions/assets when they are real OSRS items.
- If a page mixes in-scope data with excluded data, extract only the
  in-scope portion.
- If v1 scope changes, update `ignore.md`, `things.md`, and `work.md`
  together.
