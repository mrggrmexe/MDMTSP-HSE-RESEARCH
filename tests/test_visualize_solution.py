from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path("/mnt/data/scripts/visualize_solution.py")


class VisualizeSolutionScriptTests(unittest.TestCase):
    def test_generates_png_from_run_and_instance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = Path(tmp_dir)
            instance_path = root / "instance.json"
            run_path = root / "run.json"
            output_path = root / "plot.png"

            instance_payload = {
                "name": "toy_instance",
                "return_to_depot": True,
                "salesman_count": 2,
                "depots": [
                    {"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1},
                    {"id": 1, "x": 10.0, "y": 0.0, "salesmen": 1},
                ],
                "customers": [
                    {"id": 2, "x": 1.0, "y": 1.0},
                    {"id": 3, "x": 2.0, "y": 2.0},
                    {"id": 4, "x": 11.0, "y": 1.0},
                ],
            }
            run_payload = {
                "algorithm_id": "nearest_neighbour",
                "objective": 42.0,
                "feasible": True,
                "execution": {"wall_time_ms": 1.25},
                "instance_path": str(instance_path),
                "routes": [
                    {"depot_id": 0, "nodes": [0, 2, 3, 0]},
                    {"depot_id": 1, "nodes": [1, 4, 1]},
                ],
            }

            instance_path.write_text(json.dumps(instance_payload), encoding="utf-8")
            run_path.write_text(json.dumps(run_payload), encoding="utf-8")

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    str(run_path),
                    "--output",
                    str(output_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertTrue(output_path.exists())
            self.assertGreater(output_path.stat().st_size, 0)

    def test_reports_missing_instance_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = Path(tmp_dir)
            run_path = root / "run.json"
            run_payload = {"routes": [{"depot_id": 0, "nodes": [0, 1, 0]}]}
            run_path.write_text(json.dumps(run_payload), encoding="utf-8")

            result = subprocess.run(
                [sys.executable, str(SCRIPT_PATH), str(run_path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("instance path is missing", result.stderr)


if __name__ == "__main__":
    unittest.main()
