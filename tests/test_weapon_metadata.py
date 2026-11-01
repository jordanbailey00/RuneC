"""Reviewed weapon updates preserve all unrelated installed item records."""
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import export_items


class WeaponMetadataTests(unittest.TestCase):
    def test_notes_stack_without_a_raw_cache_stackable_flag(self):
        for noted, stackable, expected in ((True, False, True), (True, True, True),
                                          (False, True, True), (False, False, False)):
            record = export_items.build_record({"id": 1516, "name": "Yew logs",
                                                "noted": noted, "stackable": stackable}, {})
            flags, = struct.unpack_from("<H", record, 4)
            self.assertEqual(bool(flags & export_items.F_STACKABLE), expected)
            self.assertEqual(bool(flags & export_items.F_NOTED), noted)

    def test_reviewed_family_ranges(self):
        vectors = [("Oak longbow", "bow", 10), ("Magic comp bow", "bow", 10),
                   ("Magic shortbow", "bow", 7), ("Dark bow", "bow", 10),
                   ("Rune knife", "thrown", 4), ("Black chinchompa", "chinchompas", 9),
                   ("Heavy ballista", "crossbow", 9), ("Trident of the seas", "powered_staff", 7),
                   ("Tumeken's shadow", "powered_staff", 8),
                   ("Crossbow", "crossbow", 10), ("Phoenix crossbow", "crossbow", 5),
                   ("Hunters' crossbow", "crossbow", 8),
                   ("Hunters' sunlight crossbow", "crossbow", 8),
                   ("Dragon hunter crossbow", "crossbow", 7),
                   ("Armadyl crossbow", "crossbow", 8), ("Dorgeshuun crossbow", "crossbow", 6),
                   ("Dragon halberd", "polearm", 2), ("Unreviewed bow", "bow", -1)]
        for name, kind, expected in vectors:
            with self.subTest(name=name):
                self.assertEqual(export_items.reviewed_attack_range(
                    name, export_items.WEAPON_TYPES[kind], -1), expected)

    def test_packed_range_prefers_the_cache_value(self):
        weapon = {"weapon_type": "bow", "attack_range": 10}
        record = export_items.build_record({"id": 9999, "name": "Reviewed bow", "weapon": weapon}, {})
        self.assertEqual(record[-1], 10)
        for name, kind, expected in (("Bronze dart", "thrown", 3), ("Black salamander", "salamander", 1),
                                     ("Iban's staff", "staff", 1)):
            with self.subTest(name=name):
                wp = {"weapon_type": kind, "attack_range": -1}
                self.assertEqual(export_items.build_record({"id": 1, "name": name, "weapon": wp}, {})[-1], expected)

    def test_weapon_table_covers_known_types(self):
        for item_id, row in export_items.WEAPON_TABLE.items():
            self.assertIn(row["weapon_type"], export_items.WEAPON_TYPES, item_id)
        self.assertEqual(export_items.WEAPON_TABLE[34715]["weapon_type"], "chainmace")
        self.assertEqual(export_items.WEAPON_TABLE[33718]["weapon_type"], "slash_flail")

    def test_cache_bonuses_read_params(self):
        bonuses = export_items.cache_bonuses({1: 82, 10: 82, 12: 7, 299: 50, 11: 3})
        self.assertEqual((bonuses["attack_slash"], bonuses["melee_strength"]), (82, 82))
        self.assertEqual((bonuses["ranged_strength"], bonuses["magic_damage"], bonuses["prayer"]), (7, 50, 3))
        self.assertIsNone(export_items.cache_bonuses({14: 4, 434: 0}))

    def test_requirements_merge_keeps_longer_agreeing_sets(self):
        void = {"attack": 42, "strength": 42, "defence": 42, "prayer": 22}
        params = {434: 0, 436: 42, 435: 2, 437: 42}
        self.assertEqual(export_items.merged_requirements(params, void), void)
        self.assertEqual(export_items.merged_requirements({434: 2, 436: 60}, {"attack": 60}), {"strength": 60})
        self.assertEqual(export_items.merged_requirements({434: 12, 436: 62}, {"magic": 1}), {"magic": 1})
        self.assertEqual(export_items.merged_requirements({434: 4, 436: 85, 435: -1}, {"ranged": 75, "slayer": 5}),
                         {"ranged": 85, "slayer": 5})

    def test_identity_is_checked(self):
        with patch.dict(export_items.WEAPON_TABLE, {34715: {"name": "Obligator", "weapon_type": "chainmace"}}):
            with self.assertRaisesRegex(ValueError, "weapons.tsv says"):
                export_items.apply_cache_combat({"id": 34715, "name": "x"}, {"name": "wrong", "params": {}})


if __name__ == "__main__":
    unittest.main()
