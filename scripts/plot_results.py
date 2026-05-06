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
        "--algorithm-instance-type-summary-csv",
        type=Path,
        default=Path("results/tables/algorithm_instance_type_summary.csv"),
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
    return int(round(float(text)))


def as_float(value: str | None) -> float | None:
    if value is None:
        return None
    text = value.strip()
    if not text:
        return None
    return float(text)


def algorithm_sort_key(row: dict[str, str]) -> tuple[float, float, float, float, str]:
    median_gap = as_float(row.get("median_gap_to_best_observed"))
    median_time_ratio = as_float(row.get("median_time_ratio_to_fastest"))
    feasible_rate = as_float(row.get("feasible_rate"))
    median_time = as_float(row.get("median_wall_time_ms"))

    return (
        float("inf") if median_gap is None else median_gap,
        float("inf") if median_time_ratio is None else median_time_ratio,
        -(feasible_rate if feasible_rate is not None else -1.0),
        float("inf") if median_time is None else median_time,
        row["algorithm_id"],
    )


def prepare_metric(
    rows: list[dict[str, str]],
    key: str,
    scale: float,
    suffix: str,
) -> tuple[list[float], list[str]]:
    values: list[float] = []
    labels: list[str] = []

    for row in rows:
        raw = as_float(row.get(key))
        if raw is None:
            values.append(0.0)
            labels.append("n/a")
            continue

        values.append(raw * scale)

        if suffix:
            labels.append(f"{raw * scale:.2f}{suffix}")
        else:
            labels.append(f"{raw * scale:.2f}")

    return values, labels


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

    gap_values, gap_labels = prepare_metric(
        ordered,
        "median_gap_to_best_observed",
        100.0,
        "%",
    )
    time_gap_values, time_gap_labels = prepare_metric(
        ordered,
        "median_time_gap_to_fastest",
        100.0,
        "%",
    )
    feasible_values, feasible_labels = prepare_metric(
        ordered,
        "feasible_rate",
        100.0,
        "%",
    )

    runtime_values: list[float] = []
    runtime_labels: list[str] = []
    for row in ordered:
        raw = as_float(row.get("median_wall_time_ms"))
        if raw is None:
            runtime_values.append(0.0)
            runtime_labels.append("n/a")
        else:
            runtime_values.append(raw)
            runtime_labels.append(f"{raw:.3f} ms")

    total_runs = sum(as_int(row.get("runs")) or 0 for row in algorithm_rows)
    total_instances = len(instance_rows)

    fig, axes = plt.subplots(2, 2, figsize=(18, 11))

    bars = axes[0, 0].barh(algorithm_names, gap_values)
    axes[0, 0].set_title("Median objective gap to best observed")
    axes[0, 0].set_xlabel("Percent")
    annotate_barh(axes[0, 0], bars, gap_labels)

    bars = axes[0, 1].barh(algorithm_names, time_gap_values)
    axes[0, 1].set_title("Median runtime gap to fastest")
    axes[0, 1].set_xlabel("Percent")
    annotate_barh(axes[0, 1], bars, time_gap_labels)

    bars = axes[1, 0].barh(algorithm_names, feasible_values)
    axes[1, 0].set_title("Feasibility rate")
    axes[1, 0].set_xlabel("Percent")
    annotate_barh(axes[1, 0], bars, feasible_labels)

    bars = axes[1, 1].barh(algorithm_names, runtime_values)
    axes[1, 1].set_title("Median runtime")
    axes[1, 1].set_xlabel("Milliseconds")
    if any(value > 0.0 for value in runtime_values):
        axes[1, 1].set_xscale("log")
    annotate_barh(axes[1, 1], bars, runtime_labels)

    fig.suptitle(
        f"MDMTSP history comparison | runs={total_runs} | instances={total_instances}",
        fontsize=16,
    )
    fig.tight_layout(rect=(0.0, 0.0, 1.0, 0.97))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def plot_quality_vs_time(
    algorithm_rows: list[dict[str, str]],
    output_path: Path,
) -> None:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(10, 7))

    xs: list[float] = []
    ys: list[float] = []
    labels: list[str] = []

    for row in sorted(algorithm_rows, key=algorithm_sort_key):
        runtime_ms = as_float(row.get("median_wall_time_ms"))
        gap = as_float(row.get("median_gap_to_best_observed"))

        if runtime_ms is None or gap is None:
            continue
        if runtime_ms <= 0.0:
            continue

        xs.append(runtime_ms)
        ys.append(gap * 100.0)
        labels.append(row["algorithm_id"])

    if not xs:
        return

    ax.scatter(xs, ys)
    for x, y, label in zip(xs, ys, labels):
        ax.annotate(label, (x, y), xytext=(5, 5), textcoords="offset points")

    ax.set_xscale("log")
    ax.set_xlabel("Median runtime (ms, log scale)")
    ax.set_ylabel("Median objective gap to best observed (%)")
    ax.set_title("Quality vs runtime")
    fig.tight_layout()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def plot_instance_heatmap(
    *,
    algorithm_rows: list[dict[str, str]],
    algorithm_instance_rows: list[dict[str, str]],
    value_key: str,
    title: str,
    colorbar_label: str,
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
        value = as_float(row.get(value_key))
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

    ax.set_title(title)
    ax.set_ylabel("Algorithm")

    x_step = max(1, len(instance_names) // 24)
    x_positions = list(range(0, len(instance_names), x_step))
    x_labels = [instance_names[i] for i in x_positions]

    ax.set_xticks(x_positions)
    ax.set_xticklabels(x_labels, rotation=45, ha="right", fontsize=8)
    ax.set_yticks(range(len(ordered_algorithms)))
    ax.set_yticklabels(ordered_algorithms)

    cbar = fig.colorbar(image, ax=ax)
    cbar.set_label(colorbar_label)

    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def plot_instance_type_comparison(
    algorithm_rows: list[dict[str, str]],
    algorithm_instance_type_rows: list[dict[str, str]],
    output_path: Path,
) -> None:
    import matplotlib.pyplot as plt

    algorithms = [row["algorithm_id"] for row in sorted(algorithm_rows, key=algorithm_sort_key)]
    instance_types = sorted({row["instance_type"] for row in algorithm_instance_type_rows if row["instance_type"]})

    if not algorithms or not instance_types:
        return

    gap_lookup: dict[tuple[str, str], float] = {}
    time_lookup: dict[tuple[str, str], float] = {}

    for row in algorithm_instance_type_rows:
        gap = as_float(row.get("median_gap_to_best_observed"))
        time_gap = as_float(row.get("median_time_gap_to_fastest"))
        key = (row["algorithm_id"], row["instance_type"])
        if gap is not None:
            gap_lookup[key] = gap * 100.0
        if time_gap is not None:
            time_lookup[key] = time_gap * 100.0

    x_positions = list(range(len(instance_types)))
    width = 0.8 / max(1, len(algorithms))

    fig, axes = plt.subplots(2, 1, figsize=(14, 10), sharex=True)

    for algo_index, algorithm in enumerate(algorithms):
        offsets = [x + (algo_index - (len(algorithms) - 1) / 2.0) * width for x in x_positions]
        gap_values = [gap_lookup.get((algorithm, instance_type), 0.0) for instance_type in instance_types]
        time_values = [time_lookup.get((algorithm, instance_type), 0.0) for instance_type in instance_types]

        axes[0].bar(offsets, gap_values, width=width, label=algorithm)
        axes[1].bar(offsets, time_values, width=width, label=algorithm)

    axes[0].set_title("Median objective gap by instance type")
    axes[0].set_ylabel("Percent")

    axes[1].set_title("Median runtime gap to fastest by instance type")
    axes[1].set_ylabel("Percent")
    axes[1].set_xticks(x_positions)
    axes[1].set_xticklabels(instance_types)

    axes[0].legend()
    fig.tight_layout()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=220, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    args = parse_args()

    algorithm_summary_path = args.algorithm_summary_csv.resolve()
    instance_summary_path = args.instance_summary_csv.resolve()
    algorithm_instance_summary_path = args.algorithm_instance_summary_csv.resolve()
    algorithm_instance_type_summary_path = args.algorithm_instance_type_summary_csv.resolve()
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
    algorithm_instance_type_rows = (
        load_csv(algorithm_instance_type_summary_path)
        if algorithm_instance_type_summary_path.exists()
        else []
    )

    if not algorithm_rows:
        print("algorithm summary csv is empty", file=sys.stderr)
        return 1

    plot_history_comparison(
        algorithm_rows=algorithm_rows,
        instance_rows=instance_rows,
        output_path=plots_dir / "history_comparison.png",
    )
    plot_quality_vs_time(
        algorithm_rows=algorithm_rows,
        output_path=plots_dir / "quality_vs_time.png",
    )
    plot_instance_heatmap(
        algorithm_rows=algorithm_rows,
        algorithm_instance_rows=algorithm_instance_rows,
        value_key="median_gap_to_best_observed",
        title="Median objective gap to best observed by algorithm and instance",
        colorbar_label="Percent",
        output_path=plots_dir / "instance_gap_heatmap.png",
    )
    plot_instance_heatmap(
        algorithm_rows=algorithm_rows,
        algorithm_instance_rows=algorithm_instance_rows,
        value_key="median_time_gap_to_fastest",
        title="Median runtime gap to fastest by algorithm and instance",
        colorbar_label="Percent",
        output_path=plots_dir / "instance_time_heatmap.png",
    )

    if algorithm_instance_type_rows:
        plot_instance_type_comparison(
            algorithm_rows=algorithm_rows,
            algorithm_instance_type_rows=algorithm_instance_type_rows,
            output_path=plots_dir / "instance_type_comparison.png",
        )

    print(f"plots saved to {plots_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())