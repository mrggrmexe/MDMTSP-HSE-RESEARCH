from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--algorithm-summary-csv",
        type=Path,
        default=Path("results/tables/algorithm_summary.csv"),
    )
    parser.add_argument(
        "--instance-summary-csv",
        type=Path,
        default=Path("results/tables/instance_summary.csv"),
    )
    parser.add_argument(
        "--algorithm-instance-summary-csv",
        type=Path,
        default=Path("results/tables/algorithm_instance_summary.csv"),
    )
    parser.add_argument(
        "--plots-dir",
        type=Path,
        default=Path("results/plots"),
    )
    return parser.parse_args()


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def as_int(value: str | None) -> int | None:
    if value is None:
        return None
    text = value.strip()
    if not text:
        return None
    return int(float(text))


def as_float(value: str | None) -> float | None:
    if value is None:
        return None
    text = value.strip()
    if not text:
        return None
    return float(text)


def algorithm_sort_key(row: dict[str, str]) -> tuple[float, float, float, str]:
    median_gap = as_float(row.get("median_gap_to_best_observed"))
    feasible_rate = as_float(row.get("feasible_rate"))
    median_time = as_float(row.get("median_wall_time_ms"))

    return (
        float("inf") if median_gap is None else median_gap,
        -(feasible_rate if feasible_rate is not None else -1.0),
        float("inf") if median_time is None else median_time,
        row["algorithm_id"],
    )


def format_percentage_or_na(value: float | None) -> str:
    return "n/a" if value is None else f"{value * 100.0:.2f}%"

def format_ms_or_na(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.0f} ms"


def format_count_or_na(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.0f}"


def prepare_metric(
    rows: list[dict[str, str]],
    key: str,
    scale: float,
) -> tuple[list[float], list[str], list[bool]]:
    values: list[float] = []
    labels: list[str] = []
    missing: list[bool] = []

    for row in rows:
        raw = as_float(row.get(key))
        if raw is None:
            values.append(0.0)
            labels.append("n/a")
            missing.append(True)
        else:
            values.append(raw * scale)
            missing.append(False)

            if key.endswith("gap_to_best_observed") or key == "feasible_rate":
                labels.append(f"{raw * 100.0:.2f}%")
            elif key.endswith("wall_time_ms"):
                labels.append(f"{raw:.0f} ms")
            else:
                labels.append(f"{raw:.0f}")

    return values, labels, missing


def annotate_barh(ax, bars, labels: list[str]) -> None:
    widths = [bar.get_width() for bar in bars]
    max_width = max(widths) if widths else 0.0
    offset = 0.02 * max(1.0, max_width)

    for bar, label in zip(bars, labels):
        x = bar.get_width()
        y = bar.get_y() + bar.get_height() / 2.0
        ax.text(x + offset, y, label, va="center", fontsize=9)


def plot_history_comparison(
    algorithm_rows: list[dict[str, str]],
    instance_rows: list[dict[str, str]],
    output_path: Path,
) -> None:
    import matplotlib.pyplot as plt

    ordered = list(reversed(sorted(algorithm_rows, key=algorithm_sort_key)))
    algorithm_names = [row["algorithm_id"] for row in ordered]

    gap_values, gap_labels, _ = prepare_metric(
        ordered,
        "median_gap_to_best_observed",
        100.0,
    )
    feasible_values, feasible_labels, _ = prepare_metric(
        ordered,
        "feasible_rate",
        100.0,
    )
    runtime_values, runtime_labels, _ = prepare_metric(
        ordered,
        "median_wall_time_ms",
        1.0,
    )
    run_count_values, run_count_labels, _ = prepare_metric(
        ordered,
        "runs",
        1.0,
    )

    total_runs = sum(as_int(row.get("runs")) or 0 for row in algorithm_rows)
    total_instances = len(instance_rows)

    fig, axes = plt.subplots(2, 2, figsize=(18, 11))

    bars = axes[0, 0].barh(algorithm_names, gap_values)
    axes[0, 0].set_title("Median gap to best observed")
    axes[0, 0].set_xlabel("Percent")
    annotate_barh(axes[0, 0], bars, gap_labels)

    bars = axes[0, 1].barh(algorithm_names, feasible_values)
    axes[0, 1].set_title("Feasibility rate")
    axes[0, 1].set_xlabel("Percent")
    annotate_barh(axes[0, 1], bars, feasible_labels)

    bars = axes[1, 0].barh(algorithm_names, runtime_values)
    axes[1, 0].set_title("Median runtime")
    axes[1, 0].set_xlabel("Milliseconds")
    annotate_barh(axes[1, 0], bars, runtime_labels)

    bars = axes[1, 1].barh(algorithm_names, run_count_values)
    axes[1, 1].set_title("Run count")
    axes[1, 1].set_xlabel("Runs")
    annotate_barh(axes[1, 1], bars, run_count_labels)

    fig.suptitle(
        f"MDMTSP history comparison | runs={total_runs} | instances={total_instances}",
        fontsize=16,
    )
    fig.tight_layout(rect=(0.0, 0.0, 1.0, 0.97))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def plot_instance_gap_heatmap(
    algorithm_rows: list[dict[str, str]],
    algorithm_instance_rows: list[dict[str, str]],
    output_path: Path,
) -> None:
    import matplotlib.pyplot as plt

    ordered_algorithms = [row["algorithm_id"] for row in sorted(algorithm_rows, key=algorithm_sort_key)]

    unique_instances = sorted(
        {
            (
                row["instance_name"],
                as_int(row.get("customer_count")) or -1,
            )
            for row in algorithm_instance_rows
        },
        key=lambda item: (item[1], item[0]),
    )
    instance_names = [name for name, _ in unique_instances]

    matrix_lookup: dict[tuple[str, str], float] = {}
    for row in algorithm_instance_rows:
        value = as_float(row.get("median_gap_to_best_observed"))
        if value is not None:
            matrix_lookup[(row["algorithm_id"], row["instance_name"])] = value * 100.0

    matrix: list[list[float]] = []
    has_numeric = False

    for algorithm_id in ordered_algorithms:
        current_row: list[float] = []
        for instance_name in instance_names:
            value = matrix_lookup.get((algorithm_id, instance_name))
            if value is None:
                current_row.append(float("nan"))
            else:
                current_row.append(value)
                has_numeric = True
        matrix.append(current_row)

    if not matrix or not has_numeric:
        return

    width = max(12.0, 0.45 * len(instance_names))
    height = max(4.5, 0.55 * len(ordered_algorithms))

    fig, ax = plt.subplots(figsize=(width, height))
    image = ax.imshow(matrix, aspect="auto", interpolation="nearest")

    ax.set_title("Median gap to best observed by algorithm and instance")
    ax.set_ylabel("Algorithm")

    x_step = max(1, len(instance_names) // 24)
    x_positions = list(range(0, len(instance_names), x_step))
    x_labels = [instance_names[i] for i in x_positions]

    ax.set_xticks(x_positions)
    ax.set_xticklabels(x_labels, rotation=45, ha="right", fontsize=8)
    ax.set_yticks(range(len(ordered_algorithms)))
    ax.set_yticklabels(ordered_algorithms)

    cbar = fig.colorbar(image, ax=ax)
    cbar.set_label("Percent")

    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    args = parse_args()

    algorithm_summary_path = args.algorithm_summary_csv.resolve()
    instance_summary_path = args.instance_summary_csv.resolve()
    algorithm_instance_summary_path = args.algorithm_instance_summary_csv.resolve()
    plots_dir = args.plots_dir.resolve()

    if not algorithm_summary_path.exists():
        print(f"missing algorithm summary csv: {algorithm_summary_path}", file=sys.stderr)
        return 1
    if not instance_summary_path.exists():
        print(f"missing instance summary csv: {instance_summary_path}", file=sys.stderr)
        return 1
    if not algorithm_instance_summary_path.exists():
        print(f"missing algorithm-instance summary csv: {algorithm_instance_summary_path}", file=sys.stderr)
        return 1

    algorithm_rows = load_csv(algorithm_summary_path)
    instance_rows = load_csv(instance_summary_path)
    algorithm_instance_rows = load_csv(algorithm_instance_summary_path)

    if not algorithm_rows:
        print("algorithm summary csv is empty", file=sys.stderr)
        return 1

    plot_history_comparison(
        algorithm_rows=algorithm_rows,
        instance_rows=instance_rows,
        output_path=plots_dir / "history_comparison.png",
    )
    plot_instance_gap_heatmap(
        algorithm_rows=algorithm_rows,
        algorithm_instance_rows=algorithm_instance_rows,
        output_path=plots_dir / "instance_gap_heatmap.png",
    )

    print(f"plots saved to {plots_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())