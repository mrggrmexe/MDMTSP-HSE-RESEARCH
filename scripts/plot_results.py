from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


DEFAULT_TABLES_ROOT = Path("results/tables")
DEFAULT_RUNS_ROOT = Path("results/runs")
DEFAULT_OUTPUT_ROOT = Path("results/plots")
DEFAULT_MAX_SOLUTION_CUSTOMERS = 10_000


@dataclass(frozen=True)
class RenderTask:
    run_json: str
    output_png: str
    max_customers: int
    overwrite: bool
    dpi: int
    figure_width: float
    figure_height: float
    annotate_depots: bool
    no_legend: bool
    route_color_mode: str


@dataclass(frozen=True)
class RenderResult:
    run_json: str
    output_png: str
    status: str
    customer_count: int | None
    algorithm_id: str | None
    instance_name: str | None
    message: str


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate research plots and solution route visualizations for MDMTSP runs."
    )
    parser.add_argument("--tables-root", type=Path, default=DEFAULT_TABLES_ROOT)
    parser.add_argument("--runs-root", type=Path, default=DEFAULT_RUNS_ROOT)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--overwrite", action="store_true")

    parser.add_argument(
        "--solution-visualizations",
        dest="solution_visualizations",
        action="store_true",
        default=True,
        help="Render route maps for run.json files. Enabled by default.",
    )
    parser.add_argument(
        "--no-solution-visualizations",
        dest="solution_visualizations",
        action="store_false",
        help="Disable route-map rendering and generate only aggregate plots.",
    )
    parser.add_argument(
        "--max-solution-customers",
        type=int,
        default=DEFAULT_MAX_SOLUTION_CUSTOMERS,
        help="Render route maps only for instances with customer_count <= this value.",
    )
    parser.add_argument(
        "--solution-workers",
        type=int,
        default=max(1, min(4, (os.cpu_count() or 2) // 2)),
        help="Number of worker processes for solution rendering.",
    )
    parser.add_argument("--solution-dpi", type=int, default=180)
    parser.add_argument("--solution-figure-width", type=float, default=16.0)
    parser.add_argument("--solution-figure-height", type=float, default=9.0)
    parser.add_argument("--annotate-depots", action="store_true")
    parser.add_argument("--no-solution-legend", action="store_true")
    parser.add_argument(
        "--solution-route-color-mode",
        choices=("auto", "route", "depot", "mono"),
        default="auto",
    )
    parser.add_argument(
        "--skip-aggregate-plots",
        action="store_true",
        help="Only render solution maps; skip aggregate summary figures.",
    )
    return parser.parse_args(argv)


def read_csv_dicts(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as file:
        return list(csv.DictReader(file))


def as_float(row: dict[str, str], key: str) -> float | None:
    value = row.get(key)
    if value is None or value == "":
        return None
    try:
        result = float(value)
    except ValueError:
        return None
    if not math.isfinite(result):
        return None
    return result


def as_int(row: dict[str, str], key: str) -> int | None:
    value = row.get(key)
    if value is None or value == "":
        return None
    try:
        return int(float(value))
    except ValueError:
        return None


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def _finish_plot(output_path: Path, dpi: int = 180) -> None:
    ensure_parent(output_path)
    plt.tight_layout()
    plt.savefig(output_path, dpi=dpi)
    plt.close()


def plot_algorithm_gap_summary(tables_root: Path, output_root: Path) -> Path | None:
    rows = read_csv_dicts(tables_root / "algorithm_summary.csv")
    points: list[tuple[str, float, float | None]] = []
    for row in rows:
        algorithm = row.get("algorithm_id", "")
        median_gap = as_float(row, "median_gap_to_best_observed")
        mean_gap = as_float(row, "mean_gap_to_best_observed")
        if algorithm and median_gap is not None:
            points.append((algorithm, median_gap, mean_gap))
    if not points:
        return None

    points.sort(key=lambda item: item[1])
    labels = [item[0] for item in points]
    medians = [100.0 * item[1] for item in points]
    means = [100.0 * item[2] if item[2] is not None else np.nan for item in points]

    output_path = output_root / "algorithm_gap_summary.png"
    x = np.arange(len(labels))
    plt.figure(figsize=(max(8.0, len(labels) * 1.15), 5.2))
    plt.bar(x, medians, label="median gap")
    if any(math.isfinite(v) for v in means):
        plt.plot(x, means, marker="o", linewidth=1.5, label="mean gap")
    plt.xticks(x, labels, rotation=30, ha="right")
    plt.ylabel("gap to best observed, %")
    plt.title("Algorithm quality summary")
    plt.grid(axis="y", linewidth=0.4, alpha=0.35)
    plt.legend()
    _finish_plot(output_path)
    return output_path


def plot_time_quality_tradeoff(tables_root: Path, output_root: Path) -> Path | None:
    rows = read_csv_dicts(tables_root / "algorithm_summary.csv")
    points: list[tuple[str, float, float, int | None]] = []
    for row in rows:
        algorithm = row.get("algorithm_id", "")
        median_gap = as_float(row, "median_gap_to_best_observed")
        median_time = as_float(row, "median_wall_time_ms")
        runs = as_int(row, "runs")
        if algorithm and median_gap is not None and median_time is not None:
            points.append((algorithm, 100.0 * median_gap, median_time, runs))
    if not points:
        return None

    output_path = output_root / "time_quality_tradeoff.png"
    plt.figure(figsize=(8.2, 5.8))
    for algorithm, gap, time_ms, runs in points:
        size = 60.0 if runs is None else max(60.0, min(320.0, 20.0 + 4.0 * runs))
        plt.scatter(time_ms, gap, s=size, alpha=0.75)
        plt.annotate(algorithm, (time_ms, gap), xytext=(5, 4), textcoords="offset points", fontsize=9)
    plt.xlabel("median wall time, ms")
    plt.ylabel("median gap to best observed, %")
    plt.title("Quality/time trade-off")
    plt.grid(True, linewidth=0.4, alpha=0.35)
    if any(p[2] > 0 for p in points):
        plt.xscale("log")
    _finish_plot(output_path)
    return output_path


def plot_instance_gap_heatmap(tables_root: Path, output_root: Path, max_instances: int = 80) -> Path | None:
    rows = read_csv_dicts(tables_root / "algorithm_instance_summary.csv")
    if not rows:
        return None

    algorithms = sorted({row.get("algorithm_id", "") for row in rows if row.get("algorithm_id")})
    instances = sorted({row.get("instance_name", "") for row in rows if row.get("instance_name")})
    if not algorithms or not instances:
        return None

    if len(instances) > max_instances:
        def instance_size(name: str) -> tuple[int, str]:
            for token in name.split("_"):
                if token.isdigit():
                    return (int(token), name)
            return (10**9, name)
        instances = sorted(instances, key=instance_size)[:max_instances]

    alg_index = {name: idx for idx, name in enumerate(algorithms)}
    inst_index = {name: idx for idx, name in enumerate(instances)}
    matrix = np.full((len(algorithms), len(instances)), np.nan, dtype=float)
    for row in rows:
        alg = row.get("algorithm_id", "")
        inst = row.get("instance_name", "")
        gap = as_float(row, "median_gap_to_best_observed")
        if alg in alg_index and inst in inst_index and gap is not None:
            matrix[alg_index[alg], inst_index[inst]] = 100.0 * gap

    output_path = output_root / "instance_gap_heatmap.png"
    width = max(12.0, min(28.0, len(instances) * 0.28))
    height = max(4.8, len(algorithms) * 0.72)
    plt.figure(figsize=(width, height))
    image = plt.imshow(matrix, aspect="auto", interpolation="nearest")
    plt.colorbar(image, label="median gap, %")
    plt.yticks(np.arange(len(algorithms)), algorithms)
    tick_step = max(1, len(instances) // 30)
    shown_ticks = np.arange(0, len(instances), tick_step)
    plt.xticks(shown_ticks, [instances[idx] for idx in shown_ticks], rotation=75, ha="right", fontsize=7)
    plt.title("Instance-level quality heatmap")
    _finish_plot(output_path, dpi=170)
    return output_path


def plot_instance_type_summary(tables_root: Path, output_root: Path) -> Path | None:
    rows = read_csv_dicts(tables_root / "algorithm_instance_type_summary.csv")
    if not rows:
        return None

    algorithms = sorted({row.get("algorithm_id", "") for row in rows if row.get("algorithm_id")})
    instance_types = sorted({row.get("instance_type", "") for row in rows if row.get("instance_type")})
    if not algorithms or not instance_types:
        return None

    values = {alg: [np.nan] * len(instance_types) for alg in algorithms}
    type_index = {name: idx for idx, name in enumerate(instance_types)}
    for row in rows:
        alg = row.get("algorithm_id", "")
        typ = row.get("instance_type", "")
        gap = as_float(row, "median_gap_to_best_observed")
        if alg in values and typ in type_index and gap is not None:
            values[alg][type_index[typ]] = 100.0 * gap

    output_path = output_root / "instance_type_gap_summary.png"
    x = np.arange(len(instance_types))
    width = 0.82 / max(1, len(algorithms))
    plt.figure(figsize=(max(9.0, len(instance_types) * 1.2), 5.8))
    for idx, alg in enumerate(algorithms):
        offset = (idx - (len(algorithms) - 1) / 2.0) * width
        plt.bar(x + offset, values[alg], width=width, label=alg)
    plt.xticks(x, instance_types, rotation=20, ha="right")
    plt.ylabel("median gap to best observed, %")
    plt.title("Quality by instance type")
    plt.grid(axis="y", linewidth=0.4, alpha=0.35)
    plt.legend()
    _finish_plot(output_path)
    return output_path


def plot_history_comparison(tables_root: Path, output_root: Path) -> Path | None:
    rows = read_csv_dicts(tables_root.parent / "history" / "all_runs.csv")
    if not rows:
        rows = read_csv_dicts(tables_root / "algorithm_instance_summary.csv")
    if not rows:
        return None

    grouped: dict[str, list[float]] = {}
    for row in rows:
        alg = row.get("algorithm_id", "")
        gap = as_float(row, "gap_to_best_observed")
        if gap is None:
            gap = as_float(row, "median_gap_to_best_observed")
        if alg and gap is not None:
            grouped.setdefault(alg, []).append(100.0 * gap)
    if not grouped:
        return None

    labels = sorted(grouped)
    data = [grouped[label] for label in labels]
    output_path = output_root / "history_comparison.png"
    plt.figure(figsize=(max(8.0, len(labels) * 1.15), 5.4))
    plt.boxplot(data, labels=labels, showfliers=False)
    plt.xticks(rotation=25, ha="right")
    plt.ylabel("gap to best observed, %")
    plt.title("Distribution of solution quality over runs")
    plt.grid(axis="y", linewidth=0.4, alpha=0.35)
    _finish_plot(output_path)
    return output_path


def generate_aggregate_plots(tables_root: Path, output_root: Path) -> list[Path]:
    output_root.mkdir(parents=True, exist_ok=True)
    created: list[Path] = []
    for fn in (
        plot_algorithm_gap_summary,
        plot_time_quality_tradeoff,
        plot_instance_gap_heatmap,
        plot_instance_type_summary,
        plot_history_comparison,
    ):
        try:
            path = fn(tables_root, output_root)
        except Exception as exc:  # noqa: BLE001
            print(f"plot_results.py: failed to generate {fn.__name__}: {exc}", file=sys.stderr)
            continue
        if path is not None:
            created.append(path)
    return created


def discover_run_json_files(runs_root: Path) -> list[Path]:
    if not runs_root.exists():
        return []

    candidates = sorted(path for path in runs_root.rglob("*.json") if path.is_file())
    run_files: list[Path] = []
    for path in candidates:
        name = path.name.lower()
        if name in {"summary.json", "run_summary.json"}:
            continue
        try:
            with path.open("r", encoding="utf-8") as file:
                head = file.read(4096)
        except OSError:
            continue
        if '"routes"' in head or '"instance_path"' in head or '"algorithm_id"' in head:
            run_files.append(path)
    return run_files


def solution_output_path(run_json: Path, runs_root: Path, output_root: Path) -> Path:
    try:
        relative_parent = run_json.parent.relative_to(runs_root)
    except ValueError:
        relative_parent = Path(run_json.parent.name)

    if run_json.name == "run.json":
        file_name = "solution.png"
    else:
        file_name = f"{run_json.stem}__solution.png"
    return output_root / "solutions" / relative_parent / file_name


def _render_solution_worker(task: RenderTask) -> RenderResult:
    try:
        from visualize_solution import (  # type: ignore
            VisualizationOptions,
            VisualizeSolutionError,
            load_instance,
            load_run,
            render_solution,
            resolve_instance_path,
        )
    except Exception as exc:  # noqa: BLE001
        return RenderResult(task.run_json, task.output_png, "error", None, None, None, f"cannot import visualize_solution: {exc}")

    run_path = Path(task.run_json)
    output_path = Path(task.output_png)
    try:
        if output_path.exists() and not task.overwrite:
            return RenderResult(str(run_path), str(output_path), "skipped_existing", None, None, None, "output already exists")

        run = load_run(run_path)
        instance_path = resolve_instance_path(run, None)
        instance = load_instance(instance_path)
        if instance.customer_count > task.max_customers:
            return RenderResult(
                str(run_path),
                str(output_path),
                "skipped_too_large",
                instance.customer_count,
                run.algorithm_id,
                instance.name,
                f"customer_count={instance.customer_count} > {task.max_customers}",
            )

        options = VisualizationOptions(
            output_path=output_path,
            dpi=task.dpi,
            figure_width=task.figure_width,
            figure_height=task.figure_height,
            annotate_depots=task.annotate_depots,
            show_legend=not task.no_legend,
            equal_aspect=True,
            route_color_mode=task.route_color_mode,
            line_width=None,
            customer_size=None,
            depot_size=None,
            title=None,
            transparent=False,
            pad_fraction=0.04,
        )
        render_solution(run, instance, options)
        return RenderResult(str(run_path), str(output_path), "rendered", instance.customer_count, run.algorithm_id, instance.name, "ok")
    except Exception as exc:  # noqa: BLE001
        return RenderResult(str(run_path), str(output_path), "error", None, None, None, str(exc))


def write_solution_manifest(results: Sequence[RenderResult], output_root: Path) -> Path:
    path = output_root / "solutions" / "solution_render_manifest.csv"
    ensure_parent(path)
    fieldnames = ["status", "run_json", "output_png", "customer_count", "algorithm_id", "instance_name", "message"]
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        for result in results:
            writer.writerow(
                {
                    "status": result.status,
                    "run_json": result.run_json,
                    "output_png": result.output_png,
                    "customer_count": "" if result.customer_count is None else result.customer_count,
                    "algorithm_id": result.algorithm_id or "",
                    "instance_name": result.instance_name or "",
                    "message": result.message,
                }
            )
    return path


def render_solution_visualizations(args: argparse.Namespace) -> list[RenderResult]:
    runs_root = args.runs_root.expanduser().resolve()
    output_root = args.output_root.expanduser().resolve()
    run_files = discover_run_json_files(runs_root)
    tasks = [
        RenderTask(
            run_json=str(path.resolve()),
            output_png=str(solution_output_path(path.resolve(), runs_root, output_root)),
            max_customers=int(args.max_solution_customers),
            overwrite=bool(args.overwrite),
            dpi=int(args.solution_dpi),
            figure_width=float(args.solution_figure_width),
            figure_height=float(args.solution_figure_height),
            annotate_depots=bool(args.annotate_depots),
            no_legend=bool(args.no_solution_legend),
            route_color_mode=str(args.solution_route_color_mode),
        )
        for path in run_files
    ]

    if not tasks:
        manifest = write_solution_manifest([], output_root)
        print(f"plot_results.py: no solution JSON files found under {runs_root}")
        print(f"plot_results.py: wrote {manifest}")
        return []

    workers = max(1, int(args.solution_workers))
    results: list[RenderResult] = []
    if workers == 1:
        for task in tasks:
            result = _render_solution_worker(task)
            results.append(result)
            _print_solution_result(result)
    else:
        with ProcessPoolExecutor(max_workers=workers) as executor:
            future_to_task = {executor.submit(_render_solution_worker, task): task for task in tasks}
            for future in as_completed(future_to_task):
                result = future.result()
                results.append(result)
                _print_solution_result(result)

    results.sort(key=lambda item: item.run_json)
    manifest = write_solution_manifest(results, output_root)
    print(f"plot_results.py: wrote {manifest}")
    return results


def _print_solution_result(result: RenderResult) -> None:
    if result.status == "rendered":
        print(f"[solution] rendered {result.output_png}")
    elif result.status.startswith("skipped"):
        print(f"[solution] {result.status}: {result.run_json}")
    else:
        print(f"[solution] error: {result.run_json}: {result.message}", file=sys.stderr)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    output_root = args.output_root.expanduser().resolve()
    tables_root = args.tables_root.expanduser().resolve()

    if not args.skip_aggregate_plots:
        created = generate_aggregate_plots(tables_root, output_root)
        for path in created:
            print(f"[plot] {path}")
        if not created:
            print(f"plot_results.py: no aggregate plots generated from {tables_root}")

    if args.solution_visualizations:
        results = render_solution_visualizations(args)
        error_count = sum(1 for result in results if result.status == "error")
        if error_count:
            print(f"plot_results.py: {error_count} solution render errors; see manifest", file=sys.stderr)
            return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
