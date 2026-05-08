#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from excel_heatmaps import add_heatmap_sheets_to_workbook


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Add Gap_Heatmap and Time_Heatmap sheets to an existing Excel report."
    )
    parser.add_argument(
        "--workbook",
        type=Path,
        required=True,
        help="Path to existing .xlsx workbook.",
    )
    parser.add_argument(
        "--algorithm-instance-summary",
        type=Path,
        default=Path("results/tables/algorithm_instance_summary.csv"),
        help="Path to algorithm_instance_summary.csv",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    add_heatmap_sheets_to_workbook(
        workbook_path=args.workbook,
        algorithm_instance_summary_csv=args.algorithm_instance_summary,
    )
    print(f"added Gap_Heatmap and Time_Heatmap to {args.workbook}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
