#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from openpyxl import load_workbook


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "export_excel_report.py"


class ExportExcelReportTests(unittest.TestCase):
    def test_builds_workbook_from_timestamped_run_json(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runs_root = root / "results" / "runs" / "suite" / "algo" / "group_01" / "instance"
            logs_root = root / "results" / "logs"
            runs_root.mkdir(parents=True)
            logs_root.mkdir(parents=True)

            run_path = runs_root / "20260506T000000Z__suite__algo__instance__seed_42__abc.json"
            run_path.write_text(
                json.dumps(
                    {
                        "suite_name": "suite",
                        "algorithm_id": "algo",
                        "instance_name": "instance",
                        "instance_type": "random",
                        "instance_path": "instances/research/group_01/instance.json",
                        "seed": 42,
                        "objective": 123.5,
                        "feasible": True,
                        "success": True,
                        "wall_time_ms": 7.0,
                        "customer_count": 10,
                        "depot_count": 1,
                        "salesman_count": 1,
                        "routes": [[0, 1, 2, 0]],
                    }
                ),
                encoding="utf-8",
            )

            output = root / "report.xlsx"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--runs-root",
                    str(root / "results" / "runs"),
                    "--logs-root",
                    str(logs_root),
                    "--output",
                    str(output),
                ],
                cwd=str(ROOT),
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertTrue(output.exists())

            wb = load_workbook(output, read_only=True)
            self.assertIn("Overview", wb.sheetnames)
            self.assertIn("Runs", wb.sheetnames)
            self.assertIn("Algorithms", wb.sheetnames)

    def test_records_broken_json_as_parse_issue(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runs_root = root / "runs"
            logs_root = root / "logs"
            runs_root.mkdir()
            logs_root.mkdir()
            (runs_root / "broken.json").write_text("{not json", encoding="utf-8")

            output = root / "report.xlsx"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--runs-root",
                    str(runs_root),
                    "--logs-root",
                    str(logs_root),
                    "--output",
                    str(output),
                ],
                cwd=str(ROOT),
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            wb = load_workbook(output, read_only=True)
            self.assertIn("Parse_Issues", wb.sheetnames)
            ws = wb["Parse_Issues"]
            rows = list(ws.iter_rows(values_only=True))
            self.assertGreaterEqual(len(rows), 2)


if __name__ == "__main__":
    unittest.main()
