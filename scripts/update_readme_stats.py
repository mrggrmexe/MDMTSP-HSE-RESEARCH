#!/usr/bin/env python3

from __future__ import annotations

import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
README_PATH = REPO_ROOT / "README.md"
RUNS_ROOT = REPO_ROOT / "results" / "runs"

START_MARKER = "<!-- AUTO_STATS_START -->"
END_MARKER = "<!-- AUTO_STATS_END -->"


@dataclass
class RunRecord:
    suite_name: str
    algorithm_id: str
    instance_name: str
    objective: float | None
    wall_time_ms: float | None
    feasible: bool
    success: bool


def safe_float(value: Any) -> float | None:
    try:
        x = float(value)
    except (TypeError, ValueError):
        return None

    if not math.isfinite(x):
        return None

    return x


def load_run(path: Path) -> RunRecord | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None

    return RunRecord(
        suite_name=str(data.get("suite_name", "unknown")),
        algorithm_id=str(data.get("algorithm_id", "unknown")),
        instance_name=str(data.get("instance_name", path.stem)),
        objective=safe_float(data.get("objective")),
        wall_time_ms=safe_float(data.get("wall_time_ms")),
        feasible=bool(data.get("feasible", False)),
        success=bool(data.get("success", True)),
    )


def collect_runs() -> list[RunRecord]:
    runs: list[RunRecord] = []

    if not RUNS_ROOT.exists():
        return runs

    for path in RUNS_ROOT.rglob("*.json"):
        run = load_run(path)

        if run is None:
            continue

        runs.append(run)

    return runs


def build_stats(runs: list[RunRecord]) -> str:
    unique_instances = set()
    unique_algorithms = set()
    unique_suites = set()

    feasible_runs = 0
    successful_runs = 0

    total_wall_time_ms = 0.0

    best_objective_by_instance: dict[str, float] = {}
    fastest_time_by_instance: dict[str, float] = {}

    best_objective_wins: Counter[str] = Counter()
    fastest_time_wins: Counter[str] = Counter()

    grouped: defaultdict[str, list[RunRecord]] = defaultdict(list)

    for run in runs:
        unique_instances.add(run.instance_name)
        unique_algorithms.add(run.algorithm_id)
        unique_suites.add(run.suite_name)

        if run.feasible:
            feasible_runs += 1

        if run.success:
            successful_runs += 1

        if run.wall_time_ms is not None:
            total_wall_time_ms += run.wall_time_ms

        grouped[run.instance_name].append(run)

    for instance_name, instance_runs in grouped.items():
        feasible_objective_runs = [
            r for r in instance_runs
            if r.feasible and r.objective is not None
        ]

        if feasible_objective_runs:
            best_run = min(
                feasible_objective_runs,
                key=lambda r: r.objective,
            )

            best_objective_by_instance[instance_name] = best_run.objective
            best_objective_wins[best_run.algorithm_id] += 1

        timed_runs = [
            r for r in instance_runs
            if r.wall_time_ms is not None
        ]

        if timed_runs:
            fastest_run = min(
                timed_runs,
                key=lambda r: r.wall_time_ms,
            )

            fastest_time_by_instance[instance_name] = fastest_run.wall_time_ms
            fastest_time_wins[fastest_run.algorithm_id] += 1

    lines: list[str] = []

    lines.append(START_MARKER)
    lines.append("")
    lines.append("## Research Statistics")
    lines.append("")
    lines.append(f"- Total run artifacts: `{len(runs):,}`")
    lines.append(f"- Unique instances: `{len(unique_instances):,}`")
    lines.append(f"- Unique algorithms: `{len(unique_algorithms):,}`")
    lines.append(f"- Unique suites: `{len(unique_suites):,}`")
    lines.append(f"- Successful runs: `{successful_runs:,}`")
    lines.append(f"- Feasible runs: `{feasible_runs:,}`")
    lines.append(
        f"- Total wall time: `{total_wall_time_ms / 1000.0:,.2f} s`"
    )
    lines.append("")

    lines.append("### Best Observed Objective Wins")
    lines.append("")

    if best_objective_wins:
        for algorithm_id, wins in best_objective_wins.most_common():
            lines.append(f"- `{algorithm_id}`: `{wins}`")
    else:
        lines.append("- No feasible runs")

    lines.append("")
    lines.append("### Fastest Runtime Wins")
    lines.append("")

    if fastest_time_wins:
        for algorithm_id, wins in fastest_time_wins.most_common():
            lines.append(f"- `{algorithm_id}`: `{wins}`")
    else:
        lines.append("- No timed runs")

    lines.append("")
    lines.append(END_MARKER)

    return "\n".join(lines)


def inject_stats(readme_text: str, stats_block: str) -> str:
    if START_MARKER in readme_text and END_MARKER in readme_text:
        start = readme_text.index(START_MARKER)
        end = readme_text.index(END_MARKER) + len(END_MARKER)

        return (
            readme_text[:start]
            + stats_block
            + readme_text[end:]
        )

    lines = readme_text.splitlines()

    if not lines:
        return stats_block

    insert_index = 1

    while insert_index < len(lines) and lines[insert_index].strip() == "":
        insert_index += 1

    new_lines = (
        lines[:insert_index]
        + ["", stats_block, ""]
        + lines[insert_index:]
    )

    return "\n".join(new_lines)


def main() -> int:
    runs = collect_runs()

    stats_block = build_stats(runs)

    if README_PATH.exists():
        readme_text = README_PATH.read_text(encoding="utf-8")
    else:
        readme_text = "# MDMTSP\n"

    updated = inject_stats(readme_text, stats_block)

    README_PATH.write_text(updated, encoding="utf-8")

    print(f"Updated README statistics from {len(runs)} run artifacts.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())