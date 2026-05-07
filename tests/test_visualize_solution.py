#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "visualize_solution.py"


class VisualizeSolutionScriptTests(unittest.TestCase):
    def test_generates_png_from_run_and_instance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            instance_path = root / "instance.json"
            run_path = root / "run.json"
            output_path = root / "solution.png"

            instance_path.write_text(
                json.dumps(
                    {
                        "name": "tiny",
                        "type": "random",
                        "depots": [
                            {"id": 0, "x": 0.0, "y": 0.0, "salesmen": 1},
                            {"id": 1, "x": 10.0, "y": 0.0, "salesmen": 1},
                        ],
                        "customers": [
                            {"id": 2, "x": 1.0, "y": 1.0},
                            {"id": 3, "x": 2.0, "y": 1.0},
                            {"id": 4, "x": 9.0, "y": 1.0},
                        ],
                    }
                ),
                encoding="utf-8",
            )

            run_path.write_text(
                json.dumps(
                    {
                        "algorithm_id": "nearest_neighbour",
                        "instance_name": "tiny",
                        "instance_path": str(instance_path),
                        "objective": 42.0,
                        "feasible": True,
                        "wall_time_ms": 1.25,
                        "routes": [
                            {"depot_id": 0, "nodes": [0, 2, 3, 0]},
                            {"depot_id": 1, "nodes": [1, 4, 1]},
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(run_path), "--output", str(output_path)],
                cwd=str(ROOT),
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertTrue(output_path.exists())
            self.assertGreater(output_path.stat().st_size, 0)

    def test_reports_missing_instance_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            run_path = root / "run.json"
            output_path = root / "solution.png"
            run_path.write_text(
                json.dumps(
                    {
                        "algorithm_id": "algo",
                        "routes": [{"depot_id": 0, "nodes": [0, 1, 0]}],
                    }
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(run_path), "--output", str(output_path)],
                cwd=str(ROOT),
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("instance path is missing", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
