from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def load_aggregated_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        return list(reader)


def plot_mean_objective(rows: list[dict[str, str]], output_path: Path) -> None:
    names = [row["instance_name"] for row in rows]
    values = [float(row["mean_objective"]) for row in rows]

    plt.figure(figsize=(10, 6))
    plt.bar(names, values)
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("Mean objective")
    plt.xlabel("Instance")
    plt.title("Mean objective by instance")
    plt.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path, dpi=200)
    plt.close()


def plot_feasibility_rate(rows: list[dict[str, str]], output_path: Path) -> None:
    names = [row["instance_name"] for row in rows]
    values = [float(row["feasibility_rate"]) for row in rows]

    plt.figure(figsize=(10, 6))
    plt.bar(names, values)
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("Feasibility rate")
    plt.xlabel("Instance")
    plt.title("Feasibility rate by instance")
    plt.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path, dpi=200)
    plt.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--aggregated-csv",
        type=Path,
        default=Path("results/tables/aggregated_results.csv"),
    )
    parser.add_argument(
        "--plots-dir",
        type=Path,
        default=Path("results/plots"),
    )
    args = parser.parse_args()

    if not args.aggregated_csv.exists():
        print(f"missing aggregated csv: {args.aggregated_csv}", file=sys.stderr)
        return 1

    rows = load_aggregated_csv(args.aggregated_csv)
    if not rows:
        print("aggregated csv is empty", file=sys.stderr)
        return 1

    plot_mean_objective(rows, args.plots_dir / "mean_objective.png")
    plot_feasibility_rate(rows, args.plots_dir / "feasibility_rate.png")

    print(f"plots saved to {args.plots_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())