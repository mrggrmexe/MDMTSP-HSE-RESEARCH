#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build aggregate plots and optional solution visualizations from results/."
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
        "--max-solution-customers",
        type=int,
        default=10_000,
        help="Render route visualizations only for runs with customer_count <= this value.",
    )
    parser.add_argument(
        "--no-overwrite",
        action="store_true",
        help="Do not overwrite existing plot files.",
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
    fig, ax1 = plt.subplots(figsize=(12.0, 6.5))

    ax1.bar(x, work[gap_col], alpha=0.8)
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


def plot_instance_quality_heatmap(tables_root: Path, plots_root: Path, overwrite: bool) -> None:
    src = tables_root / "algorithm_instance_summary.csv"
    dst = plots_root / "instance_gap_heatmap.png"

    if dst.exists() and not overwrite:
        return

    df = read_csv_optional(src)
    if df is None or df.empty:
        print(
            f"plot_results.py: skipped heatmap (missing or empty {src})",
            file=sys.stderr,
        )
        return

    algo_col = pick_first_existing_column(df, ["algorithm_id", "algorithm"])
    instance_col = pick_first_existing_column(df, ["instance_name", "instance"])
    gap_col = pick_first_existing_column(
        df,
        [
            "median_gap_to_best_observed",
            "mean_gap_to_best_observed",
            "min_gap_to_best_observed",
        ],
    )

    if algo_col is None or instance_col is None or gap_col is None:
        print(
            "plot_results.py: skipped heatmap (required columns are missing)",
            file=sys.stderr,
        )
        return

    work = df[[algo_col, instance_col, gap_col]].copy()
    work[gap_col] = as_numeric(work[gap_col])
    work = work.dropna(subset=[gap_col])

    if work.empty:
        print("plot_results.py: skipped heatmap (no numeric rows)", file=sys.stderr)
        return

    pivot = (
        work.pivot_table(
            index=algo_col,
            columns=instance_col,
            values=gap_col,
            aggfunc="median",
        )
        .sort_index()
        .sort_index(axis=1)
    )

    if pivot.empty:
        print("plot_results.py: skipped heatmap (pivot is empty)", file=sys.stderr)
        return

    n_rows, n_cols = pivot.shape

    fig_width = max(14.0, min(80.0, 0.55 * n_cols + 6.0))
    fig_height = max(6.0, min(40.0, 0.90 * n_rows + 4.0))

    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    im = ax.imshow(pivot.values, aspect="auto", interpolation="nearest", cmap="viridis")

    ax.set_title("Instance-level quality heatmap", fontsize=16, pad=12)

    ax.set_yticks(np.arange(n_rows))
    ax.set_yticklabels(pivot.index, fontsize=10)

    if n_cols <= 60:
        step = 1
    elif n_cols <= 120:
        step = 2
    elif n_cols <= 240:
        step = 4
    else:
        step = max(1, n_cols // 60)

    xticks = np.arange(0, n_cols, step)
    ax.set_xticks(xticks)
    ax.set_xticklabels(
        [str(pivot.columns[i]) for i in xticks],
        rotation=75,
        ha="right",
        fontsize=8,
    )

    ax.set_xlabel("instance", fontsize=11)
    ax.set_ylabel("algorithm", fontsize=11)

    cbar = fig.colorbar(im, ax=ax, fraction=0.025, pad=0.02)
    cbar.set_label("gap to best observed, %", fontsize=11)

    ax.tick_params(axis="x", pad=2)
    ax.tick_params(axis="y", pad=2)

    fig.tight_layout()
    fig.savefig(dst, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def iter_run_jsons(runs_root: Path):
    if not runs_root.exists():
        return
    for path in sorted(runs_root.rglob("*.json")):
        yield path


def safe_load_json(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def write_solution_manifest(manifest_path: Path, rows: list[dict[str, str]]) -> None:
    with manifest_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["run_json", "status", "output_png", "reason"],
        )
        writer.writeheader()
        writer.writerows(rows)


def render_solution_visualizations(
    runs_root: Path,
    plots_root: Path,
    max_solution_customers: int,
    overwrite: bool,
) -> int:
    script_path = Path(__file__).resolve().parent / "visualize_solution.py"
    solutions_root = plots_root / "solutions"
    ensure_dir(solutions_root)

    manifest_path = solutions_root / "solution_render_manifest.csv"
    rows: list[dict[str, str]] = []
    errors = 0

    if not script_path.exists():
        rows.append(
            {
                "run_json": "",
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
            rows.append(
                {
                    "run_json": str(run_path),
                    "status": "skip",
                    "output_png": "",
                    "reason": "invalid_json",
                }
            )
            continue

        customer_count = payload.get("customer_count")
        if isinstance(customer_count, (int, float)) and int(customer_count) > max_solution_customers:
            rows.append(
                {
                    "run_json": str(run_path),
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
            rows.append(
                {
                    "run_json": str(run_path),
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
            rows.append(
                {
                    "run_json": str(run_path),
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
                    "status": "error",
                    "output_png": str(output_png),
                    "reason": (proc.stderr or proc.stdout).strip(),
                }
            )

    write_solution_manifest(manifest_path, rows)

    if errors:
        print(
            f"plot_results.py: {errors} solution render errors; see manifest\n"
            f"plot_results.py: wrote {manifest_path}",
            file=sys.stderr,
        )

    return errors


def main() -> int:
    args = parse_args()
    overwrite = not args.no_overwrite

    ensure_dir(args.plots_root)

    plot_history_comparison(args.tables_root, args.plots_root, overwrite)
    plot_instance_quality_heatmap(args.tables_root, args.plots_root, overwrite)

    if not args.no_solution_visualizations:
        render_solution_visualizations(
            runs_root=args.runs_root,
            plots_root=args.plots_root,
            max_solution_customers=args.max_solution_customers,
            overwrite=overwrite,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())