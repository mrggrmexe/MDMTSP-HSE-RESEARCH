from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


SUCCESS_STATUSES = {"ok", "success"}
EPS = 1e-9


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--search-root",
        type=Path,
        action="append",
        dest="search_roots",
        help="Directory with per-run JSON files. Can be passed multiple times.",
    )
    parser.add_argument(
        "--history-dir",
        type=Path,
        default=Path("results/history"),
    )
    parser.add_argument(
        "--tables-dir",
        type=Path,
        default=Path("results/tables"),
    )
    return parser.parse_args()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(path.name + ".tmp")
    with tmp_path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False))
            handle.write("\n")
    tmp_path.replace(path)


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(path.name + ".tmp")
    with tmp_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    tmp_path.replace(path)


def dig(data: dict[str, Any], *keys: str) -> Any:
    current: Any = data
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    return current


def first_non_none(*values: Any) -> Any:
    for value in values:
        if value is not None:
            return value
    return None


def as_str(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        text = value.strip()
        return text if text else None
    return str(value)


def as_int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        return int(text)
    return int(value)


def as_float(value: Any) -> float | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        return float(text)
    return float(value)


def as_bool(value: Any) -> bool | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    if isinstance(value, str):
        text = value.strip().lower()
        if text in {"true", "1", "yes", "y"}:
            return True
        if text in {"false", "0", "no", "n"}:
            return False
    raise ValueError(f"cannot convert to bool: {value!r}")


def safe_mean(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def safe_median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def success_from_status(status: str | None) -> bool:
    if status is None:
        return False
    return status.strip().lower() in SUCCESS_STATUSES


def approx_equal(a: float, b: float) -> bool:
    scale = max(1.0, abs(a), abs(b))
    return abs(a - b) <= EPS * scale


def normalize_result(path: Path) -> dict[str, Any]:
    data = load_json(path)
    if not isinstance(data, dict):
        raise ValueError("result JSON must be an object")

    routes = data.get("routes")
    derived_route_count = len(routes) if isinstance(routes, list) else None

    row: dict[str, Any] = {
        "run_id": as_str(first_non_none(data.get("run_id"), path.stem)) or path.stem,
        "timestamp_utc": as_str(
            first_non_none(
                data.get("timestamp_utc"),
                dig(data, "execution", "timestamp_utc"),
            )
        ),
        "algorithm_id": as_str(
            first_non_none(
                data.get("algorithm_id"),
                dig(data, "algorithm", "id"),
            )
        ) or "unknown",
        "suite_name": as_str(data.get("suite_name")),
        "instance_name": as_str(
            first_non_none(
                data.get("instance_name"),
                dig(data, "instance", "name"),
            )
        ),
        "instance_path": as_str(
            first_non_none(
                data.get("instance_path"),
                dig(data, "instance", "path"),
                dig(data, "source", "instance_path"),
            )
        ),
        "seed": as_int(
            first_non_none(
                data.get("seed"),
                dig(data, "execution", "seed"),
            )
        ),
        "improve_iterations": as_int(
            first_non_none(
                data.get("improve_iterations"),
                dig(data, "algorithm", "parameters", "improve_iterations"),
            )
        ),
        "depot_count": as_int(
            first_non_none(
                data.get("depot_count"),
                dig(data, "instance", "depot_count"),
            )
        ),
        "customer_count": as_int(
            first_non_none(
                data.get("customer_count"),
                dig(data, "instance", "customer_count"),
            )
        ),
        "salesman_count": as_int(
            first_non_none(
                data.get("salesman_count"),
                dig(data, "instance", "salesman_count"),
            )
        ),
        "return_to_depot": as_bool(
            first_non_none(
                data.get("return_to_depot"),
                dig(data, "instance", "return_to_depot"),
            )
        ),
        "objective": as_float(
            first_non_none(
                data.get("objective"),
                dig(data, "result", "objective"),
            )
        ),
        "feasible": as_bool(
            first_non_none(
                data.get("feasible"),
                dig(data, "result", "feasible"),
            )
        ),
        "status": as_str(
            first_non_none(
                data.get("status"),
                dig(data, "result", "status"),
            )
        ) or "unknown",
        "route_count": as_int(
            first_non_none(
                data.get("route_count"),
                dig(data, "result", "route_count"),
                derived_route_count,
            )
        ),
        "wall_time_ms": as_float(
            first_non_none(
                data.get("wall_time_ms"),
                dig(data, "execution", "wall_time_ms"),
            )
        ),
        "result_file": str(path),
    }

    if not row["instance_name"]:
        raise ValueError("missing instance_name")

    row["is_success"] = success_from_status(row["status"])
    row["is_comparable"] = bool(
        row["is_success"] and row["feasible"] is True and row["objective"] is not None
    )
    row["best_observed_objective"] = None
    row["gap_to_best_observed"] = None
    row["is_best_observed"] = False

    return row


def discover_result_files(search_roots: list[Path]) -> list[Path]:
    result_files: set[Path] = set()

    for root in search_roots:
        if not root.exists():
            continue

        if root.is_file():
            if root.suffix.lower() == ".json":
                result_files.add(root.resolve())
            continue

        for path in root.rglob("*.json"):
            if path.is_file():
                result_files.add(path.resolve())

    return sorted(result_files)


def annotate_best_observed(rows: list[dict[str, Any]]) -> None:
    best_by_instance: dict[str, float] = {}

    for row in rows:
        if not row["is_comparable"]:
            continue
        instance_name = row["instance_name"]
        objective = row["objective"]
        assert objective is not None

        current_best = best_by_instance.get(instance_name)
        if current_best is None or objective < current_best:
            best_by_instance[instance_name] = objective

    for row in rows:
        best = best_by_instance.get(row["instance_name"])
        row["best_observed_objective"] = best

        if not row["is_comparable"] or best is None:
            continue

        objective = row["objective"]
        assert objective is not None

        if approx_equal(best, 0.0):
            row["gap_to_best_observed"] = 0.0 if approx_equal(objective, best) else None
        else:
            row["gap_to_best_observed"] = (objective - best) / best

        row["is_best_observed"] = approx_equal(objective, best)


def build_algorithm_summary(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[row["algorithm_id"]].append(row)

    output: list[dict[str, Any]] = []

    for algorithm_id, values in grouped.items():
        gap_values = [
            row["gap_to_best_observed"]
            for row in values
            if row["gap_to_best_observed"] is not None
        ]
        runtime_values = [
            float(row["wall_time_ms"])
            for row in values
            if row["wall_time_ms"] is not None and row["is_success"]
        ]
        suites = sorted({row["suite_name"] for row in values if row["suite_name"]})
        best_instance_coverage = len(
            {row["instance_name"] for row in values if row["is_best_observed"]}
        )

        runs = len(values)
        successful_runs = sum(1 for row in values if row["is_success"])
        feasible_runs = sum(1 for row in values if row["feasible"] is True)
        comparable_runs = sum(1 for row in values if row["is_comparable"])
        best_run_count = sum(1 for row in values if row["is_best_observed"])

        output.append(
            {
                "algorithm_id": algorithm_id,
                "runs": runs,
                "successful_runs": successful_runs,
                "feasible_runs": feasible_runs,
                "feasible_rate": feasible_runs / runs if runs > 0 else None,
                "comparable_runs": comparable_runs,
                "unique_instances": len({row["instance_name"] for row in values}),
                "unique_suites": len(suites),
                "suite_names": ",".join(suites),
                "best_run_count": best_run_count,
                "best_instance_coverage": best_instance_coverage,
                "min_gap_to_best_observed": min(gap_values) if gap_values else None,
                "mean_gap_to_best_observed": safe_mean(gap_values),
                "median_gap_to_best_observed": safe_median(gap_values),
                "mean_wall_time_ms": safe_mean(runtime_values),
                "median_wall_time_ms": safe_median(runtime_values),
            }
        )

    return sorted(
        output,
        key=lambda row: (
            float("inf")
            if row["median_gap_to_best_observed"] is None
            else float(row["median_gap_to_best_observed"]),
            -int(row["best_instance_coverage"]),
            -float(row["feasible_rate"]) if row["feasible_rate"] is not None else 0.0,
            float("inf")
            if row["median_wall_time_ms"] is None
            else float(row["median_wall_time_ms"]),
            row["algorithm_id"],
        ),
    )


def build_instance_summary(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[row["instance_name"]].append(row)

    output: list[dict[str, Any]] = []

    for instance_name, values in grouped.items():
        runtime_values = [
            float(row["wall_time_ms"])
            for row in values
            if row["wall_time_ms"] is not None and row["is_success"]
        ]
        best_algorithm_ids = sorted(
            {row["algorithm_id"] for row in values if row["is_best_observed"]}
        )
        seeds = sorted({row["seed"] for row in values if row["seed"] is not None})
        runs = len(values)
        successful_runs = sum(1 for row in values if row["is_success"])
        feasible_runs = sum(1 for row in values if row["feasible"] is True)

        exemplar = values[0]
        output.append(
            {
                "instance_name": instance_name,
                "runs": runs,
                "successful_runs": successful_runs,
                "feasible_runs": feasible_runs,
                "feasible_rate": feasible_runs / runs if runs > 0 else None,
                "algorithms_tested": len({row["algorithm_id"] for row in values}),
                "seeds_tested": len(seeds),
                "best_observed_objective": exemplar["best_observed_objective"],
                "best_algorithm_ids": ",".join(best_algorithm_ids),
                "depot_count": exemplar["depot_count"],
                "customer_count": exemplar["customer_count"],
                "salesman_count": exemplar["salesman_count"],
                "return_to_depot": exemplar["return_to_depot"],
                "median_wall_time_ms": safe_median(runtime_values),
            }
        )

    return sorted(
        output,
        key=lambda row: (
            row["customer_count"] if row["customer_count"] is not None else -1,
            row["instance_name"],
        ),
    )


def build_algorithm_instance_summary(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[(row["algorithm_id"], row["instance_name"])].append(row)

    output: list[dict[str, Any]] = []

    for (algorithm_id, instance_name), values in grouped.items():
        feasible_objectives = [
            float(row["objective"])
            for row in values
            if row["objective"] is not None and row["is_comparable"]
        ]
        gap_values = [
            float(row["gap_to_best_observed"])
            for row in values
            if row["gap_to_best_observed"] is not None
        ]
        runtime_values = [
            float(row["wall_time_ms"])
            for row in values
            if row["wall_time_ms"] is not None and row["is_success"]
        ]
        suites = sorted({row["suite_name"] for row in values if row["suite_name"]})
        exemplar = values[0]
        runs = len(values)
        successful_runs = sum(1 for row in values if row["is_success"])
        feasible_runs = sum(1 for row in values if row["feasible"] is True)

        output.append(
            {
                "algorithm_id": algorithm_id,
                "instance_name": instance_name,
                "runs": runs,
                "successful_runs": successful_runs,
                "feasible_runs": feasible_runs,
                "feasible_rate": feasible_runs / runs if runs > 0 else None,
                "suite_names": ",".join(suites),
                "depot_count": exemplar["depot_count"],
                "customer_count": exemplar["customer_count"],
                "salesman_count": exemplar["salesman_count"],
                "return_to_depot": exemplar["return_to_depot"],
                "best_objective": min(feasible_objectives) if feasible_objectives else None,
                "mean_objective": safe_mean(feasible_objectives),
                "median_objective": safe_median(feasible_objectives),
                "min_gap_to_best_observed": min(gap_values) if gap_values else None,
                "mean_gap_to_best_observed": safe_mean(gap_values),
                "median_gap_to_best_observed": safe_median(gap_values),
                "mean_wall_time_ms": safe_mean(runtime_values),
                "median_wall_time_ms": safe_median(runtime_values),
            }
        )

    return sorted(
        output,
        key=lambda row: (
            row["instance_name"],
            row["algorithm_id"],
        ),
    )


def main() -> int:
    args = parse_args()

    search_roots = args.search_roots or [Path("results/runs")]
    search_roots = [root.resolve() for root in search_roots]
    history_dir = args.history_dir.resolve()
    tables_dir = args.tables_dir.resolve()

    result_files = discover_result_files(search_roots)
    if not result_files:
        print("no result JSON files found", file=sys.stderr)
        return 1

    rows: list[dict[str, Any]] = []
    skipped = 0

    for path in result_files:
        try:
            row = normalize_result(path)
        except Exception:
            skipped += 1
            continue
        rows.append(row)

    if not rows:
        print("no valid result files found", file=sys.stderr)
        return 1

    annotate_best_observed(rows)

    rows = sorted(
        rows,
        key=lambda row: (
            row["timestamp_utc"] or "",
            row["algorithm_id"],
            row["instance_name"],
            row["seed"] if row["seed"] is not None else -1,
            row["run_id"],
        ),
    )

    algorithm_summary = build_algorithm_summary(rows)
    instance_summary = build_instance_summary(rows)
    algorithm_instance_summary = build_algorithm_instance_summary(rows)

    all_runs_fieldnames = [
        "run_id",
        "timestamp_utc",
        "algorithm_id",
        "suite_name",
        "instance_name",
        "instance_path",
        "seed",
        "improve_iterations",
        "depot_count",
        "customer_count",
        "salesman_count",
        "return_to_depot",
        "objective",
        "feasible",
        "status",
        "route_count",
        "wall_time_ms",
        "is_success",
        "is_comparable",
        "best_observed_objective",
        "gap_to_best_observed",
        "is_best_observed",
        "result_file",
    ]

    algorithm_summary_fieldnames = [
        "algorithm_id",
        "runs",
        "successful_runs",
        "feasible_runs",
        "feasible_rate",
        "comparable_runs",
        "unique_instances",
        "unique_suites",
        "suite_names",
        "best_run_count",
        "best_instance_coverage",
        "min_gap_to_best_observed",
        "mean_gap_to_best_observed",
        "median_gap_to_best_observed",
        "mean_wall_time_ms",
        "median_wall_time_ms",
    ]

    instance_summary_fieldnames = [
        "instance_name",
        "runs",
        "successful_runs",
        "feasible_runs",
        "feasible_rate",
        "algorithms_tested",
        "seeds_tested",
        "best_observed_objective",
        "best_algorithm_ids",
        "depot_count",
        "customer_count",
        "salesman_count",
        "return_to_depot",
        "median_wall_time_ms",
    ]

    algorithm_instance_summary_fieldnames = [
        "algorithm_id",
        "instance_name",
        "runs",
        "successful_runs",
        "feasible_runs",
        "feasible_rate",
        "suite_names",
        "depot_count",
        "customer_count",
        "salesman_count",
        "return_to_depot",
        "best_objective",
        "mean_objective",
        "median_objective",
        "min_gap_to_best_observed",
        "mean_gap_to_best_observed",
        "median_gap_to_best_observed",
        "mean_wall_time_ms",
        "median_wall_time_ms",
    ]

    write_jsonl(history_dir / "all_runs.jsonl", rows)
    write_csv(history_dir / "all_runs.csv", rows, all_runs_fieldnames)
    write_csv(tables_dir / "algorithm_summary.csv", algorithm_summary, algorithm_summary_fieldnames)
    write_csv(tables_dir / "instance_summary.csv", instance_summary, instance_summary_fieldnames)
    write_csv(
        tables_dir / "algorithm_instance_summary.csv",
        algorithm_instance_summary,
        algorithm_instance_summary_fieldnames,
    )

    comparable_runs = sum(1 for row in rows if row["is_comparable"])
    print(f"valid run rows: {len(rows)}")
    print(f"skipped files: {skipped}")
    print(f"algorithms: {len(algorithm_summary)}")
    print(f"instances: {len(instance_summary)}")
    print(f"comparable runs: {comparable_runs}")
    return 0


if __name__ == "__main__":
    sys.exit(main())