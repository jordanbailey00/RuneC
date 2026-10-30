"""Map archive addressing helpers for modern OSRS caches."""

from __future__ import annotations

from dataclasses import dataclass

from .compose import Composition, composition_for
from .config import INDEX_MAPS
from .names import find_group_id
from .store import RcCacheStore

MAP_FILE_TERRAIN = 0
MAP_FILE_LOCATIONS = 1


@dataclass(frozen=True)
class MapRegionFiles:
    """Resolved cache locations for one mapsquare.

    A composed mapsquare (see ``compose.py``) has no cache groups: its files
    are built from copies of other mapsquares.
    """

    mapsquare: int
    region_x: int
    region_y: int
    terrain_group_id: int | None
    terrain_file_id: int | None
    location_group_id: int | None
    location_file_id: int | None
    composed: bool = False

    @property
    def has_terrain(self) -> bool:
        return self.composed or (
            self.terrain_group_id is not None and self.terrain_file_id is not None)

    @property
    def has_locations(self) -> bool:
        return self.composed or (
            self.location_group_id is not None and self.location_file_id is not None)


def mapsquare_id(region_x: int, region_y: int) -> int:
    """Return the standard mapsquare id for 64x64 region coordinates."""
    if not (0 <= region_x <= 255 and 0 <= region_y <= 255):
        msg = f"region coordinates out of range: {region_x},{region_y}"
        raise ValueError(msg)
    return (region_x << 8) | region_y


def _first_file_id(store: RcCacheStore, group_id: int | None) -> int | None:
    if group_id is None:
        return None
    file_ids = store.read_index_manifest(INDEX_MAPS).group_file_ids.get(group_id, [])
    return file_ids[0] if file_ids else None


def _resolve_named_region(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
) -> MapRegionFiles:
    manifest = store.read_index_manifest(INDEX_MAPS)
    terrain_group_id = find_group_id(manifest, f"m{region_x}_{region_y}")
    location_group_id = find_group_id(manifest, f"l{region_x}_{region_y}")
    return MapRegionFiles(
        mapsquare=mapsquare_id(region_x, region_y),
        region_x=region_x,
        region_y=region_y,
        terrain_group_id=terrain_group_id,
        terrain_file_id=_first_file_id(store, terrain_group_id),
        location_group_id=location_group_id,
        location_file_id=_first_file_id(store, location_group_id),
    )


def _resolve_unnamed_region(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
) -> MapRegionFiles:
    manifest = store.read_index_manifest(INDEX_MAPS)
    group_id = mapsquare_id(region_x, region_y)
    file_ids = manifest.group_file_ids.get(group_id, [])
    terrain_file_id = MAP_FILE_TERRAIN if MAP_FILE_TERRAIN in file_ids else None
    location_file_id = MAP_FILE_LOCATIONS if MAP_FILE_LOCATIONS in file_ids else None
    return MapRegionFiles(
        mapsquare=group_id,
        region_x=region_x,
        region_y=region_y,
        terrain_group_id=group_id if terrain_file_id is not None else None,
        terrain_file_id=terrain_file_id,
        location_group_id=group_id if location_file_id is not None else None,
        location_file_id=location_file_id,
    )


def _resolve_cache_region(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
) -> MapRegionFiles:
    manifest = store.read_index_manifest(INDEX_MAPS)
    if manifest.has_names:
        return _resolve_named_region(store, region_x, region_y)
    return _resolve_unnamed_region(store, region_x, region_y)


def _composed_region(region_x: int, region_y: int) -> MapRegionFiles:
    return MapRegionFiles(
        mapsquare=mapsquare_id(region_x, region_y),
        region_x=region_x,
        region_y=region_y,
        terrain_group_id=None,
        terrain_file_id=None,
        location_group_id=None,
        location_file_id=None,
        composed=True,
    )


def map_composition(store: RcCacheStore) -> Composition | None:
    """The composed mapsquares served for ``store``, if any."""
    return composition_for(store, _read_cache_map_file)


def find_map_region_files(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
) -> MapRegionFiles:
    """Resolve terrain/location files for one region.

    Older named indexes store terrain and locations as separate groups named
    ``mX_Y`` and ``lX_Y``. The active b237 OpenRS2 cache has an unnamed map
    index where each mapsquare is a group and files 0/1 hold terrain/location
    payloads. This helper hides that storage difference from exporters, and
    resolves composed mapsquares too.
    """
    composition = map_composition(store)
    if composition and (region_x, region_y) in composition.mapsquares:
        return _composed_region(region_x, region_y)
    return _resolve_cache_region(store, region_x, region_y)


def find_all_map_region_files(store: RcCacheStore) -> dict[int, MapRegionFiles]:
    """Return resolved map files for every mapsquare, composed ones included."""
    result = _find_all_cache_region_files(store)
    composition = map_composition(store)
    for region_x, region_y in sorted(composition.mapsquares if composition else ()):
        result[mapsquare_id(region_x, region_y)] = _composed_region(region_x, region_y)
    return result


def _find_all_cache_region_files(store: RcCacheStore) -> dict[int, MapRegionFiles]:
    manifest = store.read_index_manifest(INDEX_MAPS)
    if manifest.has_names:
        result: dict[int, MapRegionFiles] = {}
        for region_x in range(256):
            for region_y in range(256):
                entry = _resolve_named_region(store, region_x, region_y)
                if entry.has_terrain or entry.has_locations:
                    result[entry.mapsquare] = entry
        return result

    result = {}
    for group_id in manifest.group_ids:
        if not (0 <= group_id <= 0xFFFF):
            continue
        region_x = group_id >> 8
        region_y = group_id & 0xFF
        entry = _resolve_unnamed_region(store, region_x, region_y)
        if entry.has_terrain or entry.has_locations:
            result[group_id] = entry
    return result


def _map_file_kind(kind: str) -> str:
    if kind in ("terrain", "map", "m"):
        return "terrain"
    if kind in ("locations", "loc", "l", "objects"):
        return "locations"
    msg = f"unknown map file kind: {kind}"
    raise ValueError(msg)


def _read_cache_map_file(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
    kind: str,
) -> bytes | None:
    entry = _resolve_cache_region(store, region_x, region_y)
    if _map_file_kind(kind) == "terrain":
        group_id = entry.terrain_group_id
        file_id = entry.terrain_file_id
    else:
        group_id = entry.location_group_id
        file_id = entry.location_file_id

    if group_id is None or file_id is None:
        return None
    return store.read_group(INDEX_MAPS, group_id)[file_id]


def read_map_region_file(
    store: RcCacheStore,
    region_x: int,
    region_y: int,
    kind: str,
) -> bytes | None:
    """Read one terrain or location file for a region."""
    kind = _map_file_kind(kind)
    composition = map_composition(store)
    if composition and (region_x, region_y) in composition.mapsquares:
        return composition.file(region_x, region_y, kind)
    return _read_cache_map_file(store, region_x, region_y, kind)
