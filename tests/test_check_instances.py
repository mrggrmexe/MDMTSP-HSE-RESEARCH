from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path("/mnt/data/scripts/check_instances.py")
SPEC = importlib.util.spec_from_file_location("check_instances", SCRIPT_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC is not None and SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CheckInstancesTests(unittest.TestCase):
    def write_json(self, path: Path, payload: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")

    def test_valid_new_schema(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "g01_i01_c3_d2_m3_random.json"
            self.write_json(
                path,
                {
                    "name": "unit_valid",
                    "type": "euclidean",
                    "seed": 7,
                    "depots": [
                        {"id": 0, "x": 0.0, "y": 0.0, "salesmen": 2},
                        {"id": 1, "x": 10.0, "y": 10.0, "salesmen": 1},
                    ],
                    "customers": [
                        {"id": 2, "x": 1.0, "y": 1.0},
                        {"id": 3, "x": 2.0, "y": 2.0},
                        {"id": 4, "x": 3.0, "y": 3.0},
                    ],
                },
            )
            report = MODULE.validate_instance_file(path)
            self.assertEqual(report.status, "ok")
            self.assertEqual(report.schema, "new")
            self.assertEqual(report.customer_count, 3)
            self.assertEqual(report.depot_count, 2)
            self.assertEqual(report.salesmen_count, 3)

    def test_valid_legacy_schema(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "legacy_customers_3_depots_2_salesmen_3_random.json"
            self.write_json(
                path,
                {
                    "name": "legacy",
                    "salesman_count": 3,
                    "depots": [
                        {"x": 0.0, "y": 0.0},
                        {"x": 10.0, "y": 10.0},
                    ],
                    "customers": [
                        {"x": 1.0, "y": 1.0},
                        {"x": 2.0, "y": 2.0},
                        {"x": 3.0, "y": 3.0},
                    ],
                },
            )
            report = MODULE.validate_instance_file(path)
            self.assertEqual(report.status, "ok")
            self.assertEqual(report.schema, "legacy")
            self.assertEqual(report.salesmen_count, 3)

    def test_duplicate_id_is_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "bad_duplicate.json"
            self.write_json(
                path,
                {
                    "depots": [
                        {"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1},
                    ],
                    "customers": [
                        {"id": 0, "x": 1.0, "y": 1.0},
                    ],
                },
            )
            report = MODULE.validate_instance_file(path)
            self.assertEqual(report.status, "error")
            self.assertTrue(any("duplicate node id" in message for message in report.errors))

    def test_filename_mismatch_is_warning(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "g01_i01_c10_d2_m1_random.json"
            self.write_json(
                path,
                {
                    "depots": [
                        {"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1},
                        {"id": 1, "x": 10.0, "y": 0.0, "salesmen": 1},
                    ],
                    "customers": [
                        {"id": 2, "x": 1.0, "y": 0.0},
                        {"id": 3, "x": 2.0, "y": 0.0},
                    ],
                },
            )
            report = MODULE.validate_instance_file(path)
            self.assertEqual(report.status, "warning")
            self.assertTrue(any("filename customers=10" in message for message in report.warnings))
            self.assertTrue(any("filename salesmen=1" in message for message in report.warnings))

    def test_cli_writes_reports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            instance_dir = root / "instances"
            self.write_json(
                instance_dir / "good.json",
                {
                    "depots": [{"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1}],
                    "customers": [{"id": 1, "x": 1.0, "y": 1.0}],
                },
            )
            csv_path = root / "report.csv"
            jsonl_path = root / "report.jsonl"
            exit_code = MODULE.main(
                [
                    str(instance_dir),
                    "--quiet",
                    "--report-csv",
                    str(csv_path),
                    "--report-jsonl",
                    str(jsonl_path),
                ]
            )
            self.assertEqual(exit_code, 0)
            self.assertTrue(csv_path.exists())
            self.assertTrue(jsonl_path.exists())
            self.assertIn("good.json", jsonl_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
