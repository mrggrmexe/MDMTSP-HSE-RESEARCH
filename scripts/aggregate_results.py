from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def extract_result(path: Path) -> dict[str, Any]:
    data = load_json(path)

    return {
        "instance_name": str(data["instance_name"]),
        "seed": int(data["seed"]),
        "depot_count": int(data["depot_count"]),
        "customer_count": int(data["customer_count"]),
        "salesman_count": int(data["salesman_count"]),
        "return_to_depot": bool(data["return_to_depot"]),
        "objective": float(data["objective"]),
        "feasible": bool(data["feasible"]),
        "status": str(data["status"]),
        "route_count": len(data.get("routes", [])),
    }


def find_result_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*.json") if path.is_file())


def write_raw_csv(rows: list[dict[str, Any]], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "instance_name",
        "seed",
        "depot_count",
        "customer_count",
        "salesman_count",
        "return_to_depot",
        "objective",
        "feasible",
        "status",
        "route_count",
    ]

    with output_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)

    for row in rows:
        grouped[row["instance_name"]].append(row)

    output: list[dict[str, Any]] = []

    for instance_name, values in sorted(grouped.items()):
        objectives = [row["objective"] for row in values]
        feasible_count = sum(1 for row in values if row["feasible"])

        output.append(
            {
                "instance_name": instance_name,
                "runs": len(values),
                "best_objective": min(objectives),
                "worst_objective": max(objectives),
                "mean_objective": statistics.fmean(objectives),
                "median_objective": statistics.median(objectives),
                "stdev_objective": statistics.stdev(objectives) if len(objectives) > 1 else 0.0,
                "feasible_runs": feasible_count,
                "feasibility_rate": feasible_count / len(values),
                "depot_count": values[0]["depot_count"],
                "customer_count": values[0]["customer_count"],
                "salesman_count": values[0]["salesman_count"],
                "return_to_depot": values[0]["return_to_depot"],
            }
        )

    return output


def write_aggregated_csv(rows: list[dict[str, Any]], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "instance_name",
        "runs",
        "best_objective",
        "worst_objective",
        "mean_objective",
        "median_objective",
        "stdev_objective",
        "feasible_runs",
        "feasibility_rate",
        "depot_count",
        "customer_count",
        "salesman_count",
        "return_to_depot",
    ]

    with output_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", type=Path, default=Path("results"))
    parser.add_argument("--raw-csv", type=Path, default=Path("results/tables/raw_results.csv"))
    parser.add_argument("--aggregated-csv", type=Path, default=Path("results/tables/aggregated_results.csv"))
    args = parser.parse_args()

    result_files = find_result_files(args.results_dir)
    result_files = [path for path in result_files if path.name not in {"run_summary.json"}]

    rows: list[dict[str, Any]] = []
    for path in result_files:
        try:
            row = extract_result(path)
        except Exception:
            continue
        rows.append(row)

    if not rows:
        print("no valid result files found", file=sys.stderr)
        return 1

    write_raw_csv(rows, args.raw_csv)
    aggregated = aggregate(rows)
    write_aggregated_csv(aggregated, args.aggregated_csv)

    print(f"raw rows: {len(rows)}")
    print(f"aggregated rows: {len(aggregated)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())