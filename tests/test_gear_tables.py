"""Gear includes match their content tables, and the tables stay consistent."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import export_gear_tables

RONDACHE = "34719"  # a shield-slot special, the only one that isn't a weapon's


def rows(name: str) -> list[list[str]]:
    return export_gear_tables.rows(ROOT / "content/items" / name)


class GearTableTests(unittest.TestCase):
    def test_includes_are_current(self):
        subprocess.run([sys.executable, str(ROOT / "tools/export_gear_tables.py"), "--check"],
                       check=True, capture_output=True)

    def test_each_item_appears_once(self):
        for name in ("gear.tsv", "special_attacks.tsv", "weapons.tsv", "block_anims.tsv"):
            ids = [row[0] for row in rows(name)]
            self.assertEqual(len(ids), len(set(ids)), name)

    def test_special_weapons_have_weapon_rows(self):
        weapons = {row[0] for row in rows("weapons.tsv")}
        self.assertFalse({row[0] for row in rows("special_attacks.tsv")
                          if int(row[2]) > 0} - weapons - {RONDACHE})

    def test_special_costs_are_percentages(self):
        for row in rows("special_attacks.tsv"):
            self.assertTrue(0 <= int(row[2]) <= 100, row)


if __name__ == "__main__":
    unittest.main()
