#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "check_instances.py"

SPEC = importlib.util.spec_from_file_location("check_instances", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC is not None
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)
sys.modules[SPEC.name] = MODULE


class CheckInstancesTests(unittest.TestCase):
    def test_valid_modern_instance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "g01_i01_c2_d1_m1_random.json"
            path.write_text(
                json.dumps(
                    {
                        "name": "g01_i01_c2_d1_m1_random",
                        "type": "random",
                        "seed": 42,
                        "depots": [{"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1}],
                        "customers": [
                            {"id": 1, "x": 1.0, "y": 0.0},
                            {"id": 2, "x": 2.0, "y": 0.0},
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = MODULE.validate_instance_file(path)
            self.assertEqual(result.error_count, 0)
            self.assertEqual(result.customer_count, 2)
            self.assertEqual(result.depot_count, 1)
            self.assertEqual(result.salesman_count, 1)

    def test_duplicate_ids_are_errors(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text(
                json.dumps(
                    {
                        "name": "bad",
                        "depots": [{"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1}],
                        "customers": [{"id": 0, "x": 1.0, "y": 1.0}],
                    }
                ),
                encoding="utf-8",
            )

            result = MODULE.validate_instance_file(path)
            self.assertGreater(result.error_count, 0)
            self.assertTrue(any("duplicate" in issue.message.lower() for issue in result.issues))

    def test_valid_legacy_instance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "legacy_c2_d1_m1.json"
            path.write_text(
                json.dumps(
                    {
                        "name": "legacy",
                        "depots": [[0.0, 0.0]],
                        "customers": [[1.0, 0.0], [2.0, 0.0]],
                        "salesman_count": 1,
                        "return_to_depot": True,
                    }
                ),
                encoding="utf-8",
            )

            result = MODULE.validate_instance_file(path)
            self.assertEqual(result.error_count, 0)
            self.assertEqual(result.customer_count, 2)
            self.assertEqual(result.depot_count, 1)
            self.assertEqual(result.salesman_count, 1)


if __name__ == "__main__":
    unittest.main()
