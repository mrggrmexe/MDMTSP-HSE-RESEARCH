#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build lightweight aggregate plots and optional solution visualizations from results/."
    )
    parser.add_argument(
        "--tables-root",
        type=Path,
        default=Path("results/tables"),
        help="Directory with aggregated CSV tables.",
    )
    parser.add_argument(
        "--runs-root",
        type=Path,
        default=Path("results/runs"),
        help="Directory with run JSON files.",
    )
    parser.add_argument(
        "--plots-root",
        type=Path,
        default=Path("results/plots"),
        help="Output directory for generated plots.",
    )
    parser.add_argument(
        "--no-solution-visualizations",
        action="store_true",
        help="Disable per-solution PNG rendering.",
    )
    parser.add_argument(
        "--solution-algorithm",
        action="append",
        default=[],
        metavar="ALGORITHM_ID",
        help=(
            "Render solution PNGs only for this algorithm_id. "
            "Can be passed multiple times. If omitted, all algorithms are rendered."
        ),
    )
    parser.add_argument(
        "--solution-suite",
        action="append",
        default=[],
        metavar="SUITE_NAME",
        help=(
            "Render solution PNGs only for this suite_name. "
            "Can be passed multiple times. If omitted, all suites are rendered."
        ),
    )
    parser.add_argument(
        "--max-solution-customers",
        type=int,
        default=10_000,
        help="Render route visualizations only for runs with customer_count <= this value.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Compatibility flag: overwrite plot files. This is already the default unless --no-overwrite is set.",
    )
    parser.add_argument(
        "--no-overwrite",
        action="store_true",
        help="Do not overwrite existing plot files.",
    )
    parser.add_argument(
        "--keep-stale-solution-pngs",
        action="store_true",
        help="Do not delete previous solution PNGs before rendering.",
    )
    return parser.parse_args()


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def read_csv_optional(path: Path) -> pd.DataFrame | None:
    if not path.exists():
        return None
    return pd.read_csv(path)


def pick_first_existing_column(df: pd.DataFrame, candidates: list[str]) -> str | None:
    for name in candidates:
        if name in df.columns:
            return name
    return None


def as_numeric(series: pd.Series) -> pd.Series:
    return pd.to_numeric(series, errors="coerce")


def plot_history_comparison(tables_root: Path, plots_root: Path, overwrite: bool) -> None:
    src = tables_root / "algorithm_summary.csv"
    dst = plots_root / "history_comparison.png"

    if dst.exists() and not overwrite:
        return

    df = read_csv_optional(src)
    if df is None or df.empty:
        print(
            f"plot_results.py: skipped history comparison (missing or empty {src})",
            file=sys.stderr,
        )
        return

    algo_col = pick_first_existing_column(df, ["algorithm_id", "algorithm", "name"])
    gap_col = pick_first_existing_column(
        df,
        [
            "median_gap_to_best_observed",
            "mean_gap_to_best_observed",
            "min_gap_to_best_observed",
        ],
    )
    time_col = pick_first_existing_column(
        df,
        [
            "median_wall_time_ms",
            "mean_wall_time_ms",
            "p90_wall_time_ms",
        ],
    )

    if algo_col is None or gap_col is None or time_col is None:
        print(
            "plot_results.py: skipped history comparison (required columns are missing)",
            file=sys.stderr,
        )
        return

    work = df[[algo_col, gap_col, time_col]].copy()
    work[gap_col] = as_numeric(work[gap_col])
    work[time_col] = as_numeric(work[time_col])
    work = work.dropna(subset=[gap_col, time_col])

    if work.empty:
        print(
            "plot_results.py: skipped history comparison (no numeric rows)",
            file=sys.stderr,
        )
        return

    work = work.sort_values(by=[gap_col, time_col], ascending=[True, True]).reset_index(drop=True)

    x = np.arange(len(work))
    fig, ax1 = plt.subplots(figsize=(12.0, 6.2))

    ax1.bar(x, work[gap_col], alpha=0.82)
    ax1.set_ylabel("median gap to best observed, %", fontsize=11)
    ax1.set_xticks(x)
    ax1.set_xticklabels(work[algo_col], rotation=20, ha="right", fontsize=10)
    ax1.set_title("Algorithm-level comparison", fontsize=16, pad=12)
    ax1.grid(axis="y", alpha=0.25)

    ax2 = ax1.twinx()
    ax2.plot(x, work[time_col], marker="o", linewidth=2.0)
    ax2.set_ylabel("median wall time, ms", fontsize=11)

    fig.subplots_adjust(left=0.10, right=0.90, top=0.88, bottom=0.22)
    fig.savefig(dst, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def iter_run_jsons(runs_root: Path):
    if not runs_root.exists():
        return
    for path in sorted(runs_root.rglob("*.json")):
        yield path


def safe_load_json(path: Path) -> dict | None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    return payload if isinstance(payload, dict) else None


def write_solution_manifest(manifest_path: Path, rows: list[dict[str, str]]) -> None:
    with manifest_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["run_json", "algorithm_id", "suite_name", "status", "output_png", "reason"],
        )
        writer.writeheader()
        writer.writerows(rows)


def clean_solution_visualizations_root(solutions_root: Path) -> None:
    if not solutions_root.exists():
        return
    for child in solutions_root.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def path_contains_component(path: Path, component: str) -> bool:
    return component in path.parts


def clean_filtered_solution_visualizations(
    solutions_root: Path,
    runs_root: Path,
    algorithms: set[str],
    suites: set[str],
) -> None:
    if not solutions_root.exists():
        return

    for run_path in iter_run_jsons(runs_root):
        payload = safe_load_json(run_path)
        if payload is None:
            continue

        algorithm_id = str(payload.get("algorithm_id", ""))
        suite_name = str(payload.get("suite_name", ""))
        if algorithms and algorithm_id not in algorithms:
            continue
        if suites and suite_name not in suites:
            continue

        try:
            rel = run_path.relative_to(runs_root)
        except ValueError:
            continue

        output_png = (solutions_root / rel).with_suffix(".png")
        if output_png.exists():
            output_png.unlink()

    prune_empty_dirs(solutions_root)


def prune_empty_dirs(root: Path) -> None:
    if not root.exists():
        return
    for path in sorted((p for p in root.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        try:
            path.rmdir()
        except OSError:
            pass


def should_render_solution(
    payload: dict,
    algorithms: set[str],
    suites: set[str],
) -> tuple[bool, str]:
    algorithm_id = str(payload.get("algorithm_id", ""))
    suite_name = str(payload.get("suite_name", ""))

    if algorithms and algorithm_id not in algorithms:
        return False, "algorithm_mismatch"
    if suites and suite_name not in suites:
        return False, "suite_mismatch"
    return True, ""


def render_solution_visualizations(
    runs_root: Path,
    plots_root: Path,
    max_solution_customers: int,
    overwrite: bool,
    keep_stale_solution_pngs: bool,
    solution_algorithms: Iterable[str],
    solution_suites: Iterable[str],
) -> int:
    script_path = Path(__file__).resolve().parent / "visualize_solution.py"
    solutions_root = plots_root / "solutions"
    ensure_dir(solutions_root)

    algorithms = {item for item in solution_algorithms if item}
    suites = {item for item in solution_suites if item}

    if overwrite and not keep_stale_solution_pngs:
        if algorithms or suites:
            clean_filtered_solution_visualizations(solutions_root, runs_root, algorithms, suites)
        else:
            clean_solution_visualizations_root(solutions_root)
            ensure_dir(solutions_root)

    manifest_path = solutions_root / "solution_render_manifest.csv"
    rows: list[dict[str, str]] = []
    errors = 0
    rendered = 0
    skipped = 0

    if not script_path.exists():
        rows.append(
            {
                "run_json": "",
                "algorithm_id": "",
                "suite_name": "",
                "status": "error",
                "output_png": "",
                "reason": f"visualize_solution.py not found: {script_path}",
            }
        )
        write_solution_manifest(manifest_path, rows)
        print(
            f"plot_results.py: visualize_solution.py not found; wrote {manifest_path}",
            file=sys.stderr,
        )
        return 1

    for run_path in iter_run_jsons(runs_root):
        payload = safe_load_json(run_path)
        if payload is None:
            skipped += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": "",
                    "suite_name": "",
                    "status": "skip",
                    "output_png": "",
                    "reason": "invalid_json",
                }
            )
            continue

        algorithm_id = str(payload.get("algorithm_id", ""))
        suite_name = str(payload.get("suite_name", ""))
        should_render, reason = should_render_solution(payload, algorithms, suites)
        if not should_render:
            skipped += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": algorithm_id,
                    "suite_name": suite_name,
                    "status": "skip",
                    "output_png": "",
                    "reason": reason,
                }
            )
            continue

        customer_count = payload.get("customer_count")
        if isinstance(customer_count, (int, float)) and int(customer_count) > max_solution_customers:
            skipped += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": algorithm_id,
                    "suite_name": suite_name,
                    "status": "skip",
                    "output_png": "",
                    "reason": f"customer_count>{max_solution_customers}",
                }
            )
            continue

        rel = run_path.relative_to(runs_root)
        output_png = (solutions_root / rel).with_suffix(".png")
        ensure_dir(output_png.parent)

        if output_png.exists() and not overwrite:
            skipped += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": algorithm_id,
                    "suite_name": suite_name,
                    "status": "skip",
                    "output_png": str(output_png),
                    "reason": "exists",
                }
            )
            continue

        proc = subprocess.run(
            [sys.executable, str(script_path), str(run_path), "--output", str(output_png)],
            text=True,
            capture_output=True,
            check=False,
        )

        if proc.returncode == 0:
            rendered += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": algorithm_id,
                    "suite_name": suite_name,
                    "status": "rendered",
                    "output_png": str(output_png),
                    "reason": "",
                }
            )
        else:
            errors += 1
            rows.append(
                {
                    "run_json": str(run_path),
                    "algorithm_id": algorithm_id,
                    "suite_name": suite_name,
                    "status": "error",
                    "output_png": str(output_png),
                    "reason": (proc.stderr or proc.stdout).strip(),
                }
            )

    write_solution_manifest(manifest_path, rows)

    filter_msg = []
    if algorithms:
        filter_msg.append("algorithms=" + ",".join(sorted(algorithms)))
    if suites:
        filter_msg.append("suites=" + ",".join(sorted(suites)))
    filter_suffix = f" ({'; '.join(filter_msg)})" if filter_msg else ""

    print(
        f"plot_results.py: solution visualizations{filter_suffix}: "
        f"rendered={rendered}, skipped={skipped}, errors={errors}; wrote {manifest_path}",
        file=sys.stderr,
    )

    if errors:
        print(
            f"plot_results.py: {errors} solution render errors; see manifest",
            file=sys.stderr,
        )

    return errors


def main() -> int:
    args = parse_args()
    overwrite = args.overwrite or not args.no_overwrite

    ensure_dir(args.plots_root)

    plot_history_comparison(args.tables_root, args.plots_root, overwrite)

    if not args.no_solution_visualizations:
        render_solution_visualizations(
            runs_root=args.runs_root,
            plots_root=args.plots_root,
            max_solution_customers=args.max_solution_customers,
            overwrite=overwrite,
            keep_stale_solution_pngs=args.keep_stale_solution_pngs,
            solution_algorithms=args.solution_algorithm,
            solution_suites=args.solution_suite,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
