"""Known cache index and config group constants."""

from __future__ import annotations

from .store import RcCacheStore

INDEX_FRAMES = 0
INDEX_SKELETONS = 1
INDEX_CONFIGS = 2
INDEX_INTERFACES = 3
INDEX_MAPS = 5
INDEX_MODELS = 7
INDEX_SPRITES = 8
INDEX_TEXTURES = 9
INDEX_CLIENTSCRIPTS = 12

CONFIG_OBJECT = 6
CONFIG_ENUM = 8
CONFIG_NPC = 9
CONFIG_ITEM = 10
CONFIG_PARAM = 11
CONFIG_SEQUENCE = 12
CONFIG_SPOTANIM = 13
CONFIG_VARBIT = 14
CONFIG_VARP = 16

CONFIG_GROUP_NAMES = {
    CONFIG_OBJECT: "object",
    CONFIG_ENUM: "enum",
    CONFIG_NPC: "npc",
    CONFIG_ITEM: "item",
    CONFIG_PARAM: "param",
    CONFIG_SEQUENCE: "sequence",
    CONFIG_SPOTANIM: "spotanim",
    CONFIG_VARBIT: "varbit",
    CONFIG_VARP: "varp",
}


def read_config_group(store: RcCacheStore, group_id: int) -> dict[int, bytes]:
    return store.read_group(INDEX_CONFIGS, group_id)


def read_config_entry(store: RcCacheStore, group_id: int, entry_id: int) -> bytes:
    files = read_config_group(store, group_id)
    if entry_id not in files:
        name = CONFIG_GROUP_NAMES.get(group_id, str(group_id))
        msg = f"{name} config entry {entry_id} not found"
        raise KeyError(msg)
    return files[entry_id]
