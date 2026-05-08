#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import random
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


GROUP_PREFIX = "g03"
DEFAULT_OUTPUT_DIR = Path("instances/research/group_03")
DEFAULT_CUSTOMERS = 500

COUNTS = {
    "overlapping_cluster": 12,
    "bridge_line": 8,
    "asymmetric_depot": 8,
    "mixed_density": 6,
    "outlier_heavy": 6,
}


@dataclass(frozen=True)
class InstanceSpec:
    family: str
    customers: int
    depots: int
    salesmen: int
    seed: int


def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def sample_gaussian_point(rng: random.Random, cx: float, cy: float, sigma: float) -> tuple[float, float]:
    return rng.gauss(cx, sigma), rng.gauss(cy, sigma)


def sample_disc_point(rng: random.Random, cx: float, cy: float, radius: float) -> tuple[float, float]:
    angle = rng.uniform(0.0, 2.0 * math.pi)
    rr = radius * math.sqrt(rng.random())
    return cx + rr * math.cos(angle), cy + rr * math.sin(angle)


def sample_segment_point(
    rng: random.Random,
    ax: float,
    ay: float,
    bx: float,
    by: float,
    noise: float,
) -> tuple[float, float]:
    t = rng.random()
    return (
        ax + (bx - ax) * t + rng.gauss(0.0, noise),
        ay + (by - ay) * t + rng.gauss(0.0, noise),
    )


def normalize_points(points: list[tuple[float, float]], width: float = 1000.0, height: float = 1000.0) -> list[tuple[float, float]]:
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]

    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    span_x = max(max_x - min_x, 1e-9)
    span_y = max(max_y - min_y, 1e-9)

    scale = min(width / span_x, height / span_y) * 0.92

    return [
        (
            clamp((x - min_x) * scale + width * 0.04, 0.0, width),
            clamp((y - min_y) * scale + height * 0.04, 0.0, height),
        )
        for x, y in points
    ]


def allocate_salesmen(rng: random.Random, depots: int, salesmen: int, bias: list[float]) -> list[int]:
    if salesmen < depots:
        raise ValueError("salesmen must be >= depots")

    if len(bias) < depots:
        bias = bias + [1.0] * (depots - len(bias))

    weights = bias[:depots]
    alloc = [1] * depots
    extra = salesmen - depots

    total = sum(weights)
    probs = [w / total for w in weights]

    for _ in range(extra):
        u = rng.random()
        acc = 0.0
        chosen = depots - 1

        for i, p in enumerate(probs):
            acc += p
            if u <= acc:
                chosen = i
                break

        alloc[chosen] += 1

    return alloc


def build_instance_json(
    name: str,
    seed: int,
    depots_xy: list[tuple[float, float]],
    customers_xy: list[tuple[float, float]],
    salesmen_per_depot: list[int],
    family: str,
) -> dict:
    depots = [
        {
            "id": depot_id,
            "x": round(x, 6),
            "y": round(y, 6),
            "salesmen": int(salesmen),
        }
        for depot_id, ((x, y), salesmen) in enumerate(zip(depots_xy, salesmen_per_depot, strict=True))
    ]

    customers = [
        {
            "id": len(depots_xy) + idx,
            "x": round(x, 6),
            "y": round(y, 6),
        }
        for idx, (x, y) in enumerate(customers_xy)
    ]

    return {
        "name": name,
        "type": "euclidean",
        "seed": int(seed),
        "distribution": "adversarial",
        "adversarial_family": family,
        "depots": depots,
        "customers": customers,
    }


def gen_overlapping_cluster(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    centers = [(0.0, 0.0), (110.0, 20.0), (55.0, 80.0)]
    sigmas = [28.0, 32.0, 26.0]
    weights = [0.40, 0.34, 0.26]

    customers = []

    for _ in range(spec.customers):
        u = rng.random()
        acc = 0.0
        cluster = 0

        for i, w in enumerate(weights):
            acc += w
            if u <= acc:
                cluster = i
                break

        x, y = sample_gaussian_point(rng, *centers[cluster], sigmas[cluster])

        if rng.random() < 0.12:
            mix = (cluster + rng.randint(1, 2)) % 3
            x2, y2 = sample_gaussian_point(rng, *centers[mix], sigmas[mix] * 0.85)
            x = 0.55 * x + 0.45 * x2
            y = 0.55 * y + 0.45 * y2

        customers.append((x, y))

    depots = [
        (-25.0, -10.0),
        (130.0, 8.0),
        (48.0, 108.0),
        (55.0, 35.0),
        (-85.0, 75.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.0, 1.0, 1.0, 1.5, 1.2])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_bridge_line(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []

    left_n = int(spec.customers * 0.38)
    right_n = int(spec.customers * 0.38)
    bridge_n = int(spec.customers * 0.18)
    line_n = spec.customers - left_n - right_n - bridge_n

    for _ in range(left_n):
        customers.append(sample_gaussian_point(rng, -140.0, 0.0, 22.0))

    for _ in range(right_n):
        customers.append(sample_gaussian_point(rng, 140.0, 0.0, 22.0))

    for _ in range(bridge_n):
        customers.append(sample_segment_point(rng, -90.0, -10.0, 90.0, 10.0, 6.0))

    for _ in range(line_n):
        customers.append(sample_segment_point(rng, -175.0, 55.0, 175.0, -55.0, 4.0))

    depots = [
        (-175.0, -45.0),
        (175.0, 45.0),
        (0.0, 70.0),
        (0.0, -70.0),
        (85.0, -95.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.4, 1.4, 0.8, 0.8, 1.0])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_asymmetric_depot(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    centers = [(-60.0, -15.0), (25.0, 30.0), (105.0, 0.0), (35.0, -75.0)]
    weights = [0.46, 0.22, 0.18, 0.14]

    customers = []

    for _ in range(spec.customers):
        u = rng.random()
        acc = 0.0
        cluster = 0

        for i, w in enumerate(weights):
            acc += w
            if u <= acc:
                cluster = i
                break

        sigma = 18.0 if cluster == 0 else 14.0
        customers.append(sample_gaussian_point(rng, *centers[cluster], sigma))

    depots = [
        (-120.0, -95.0),
        (130.0, 85.0),
        (145.0, -85.0),
        (5.0, 110.0),
        (-20.0, 70.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [3.2, 0.8, 0.7, 0.7, 1.0])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_mixed_density(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []

    dense_n = int(spec.customers * 0.55)
    medium_n = int(spec.customers * 0.28)
    sparse_n = spec.customers - dense_n - medium_n

    for _ in range(dense_n):
        customers.append(sample_gaussian_point(rng, -30.0, 20.0, 8.0))

    for _ in range(medium_n):
        customers.append(sample_gaussian_point(rng, 80.0, -10.0, 28.0))

    for _ in range(sparse_n):
        customers.append((rng.uniform(-160.0, 160.0), rng.uniform(-120.0, 120.0)))

    depots = [
        (-70.0, -40.0),
        (110.0, 20.0),
        (5.0, 95.0),
        (150.0, -70.0),
        (-120.0, 85.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.6, 1.2, 0.9, 0.8, 1.0])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_outlier_heavy(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []

    core_n = int(spec.customers * 0.72)
    near_outlier_n = int(spec.customers * 0.14)
    far_outlier_n = spec.customers - core_n - near_outlier_n

    for _ in range(core_n):
        customers.append(sample_gaussian_point(rng, 0.0, 0.0, 22.0))

    for _ in range(near_outlier_n):
        customers.append(sample_disc_point(rng, 0.0, 0.0, 145.0))

    for _ in range(far_outlier_n):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        radius = rng.uniform(220.0, 310.0)
        customers.append((radius * math.cos(angle), radius * math.sin(angle)))

    depots = [
        (-35.0, -25.0),
        (40.0, 20.0),
        (0.0, 85.0),
        (0.0, -95.0),
        (120.0, 0.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.1, 1.1, 0.9, 0.9, 1.0])

    return normalize_points(depots), normalize_points(customers), salesmen


GENERATORS: dict[str, Callable] = {
    "overlapping_cluster": gen_overlapping_cluster,
    "bridge_line": gen_bridge_line,
    "asymmetric_depot": gen_asymmetric_depot,
    "mixed_density": gen_mixed_density,
    "outlier_heavy": gen_outlier_heavy,
}


def next_group03_index(output_dir: Path) -> int:
    pattern = re.compile(r"^g03_i(\d+)_")
    max_seen = 0

    if output_dir.exists():
        for path in output_dir.glob("g03_i*.json"):
            match = pattern.match(path.name)
            if match:
                max_seen = max(max_seen, int(match.group(1)))

    return max_seen + 1


def make_specs(base_seed: int, customers: int) -> list[InstanceSpec]:
    specs = []
    offset = 0

    variants = {
        "overlapping_cluster": [(4, 12), (4, 13), (5, 15)],
        "bridge_line": [(4, 12), (5, 14)],
        "asymmetric_depot": [(4, 13), (5, 16)],
        "mixed_density": [(4, 12), (5, 14)],
        "outlier_heavy": [(4, 12), (4, 13), (5, 15)],
    }

    for family, count in COUNTS.items():
        for i in range(count):
            depots, salesmen = variants[family][i % len(variants[family])]
            specs.append(
                InstanceSpec(
                    family=family,
                    customers=customers,
                    depots=depots,
                    salesmen=salesmen,
                    seed=base_seed + offset,
                )
            )
            offset += 1

    return specs


def write_instance(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate 40 adversarial MDMTSP instances for instances/research/group_03"
    )
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--base-seed", type=int, default=203600)
    parser.add_argument("--customers", type=int, default=DEFAULT_CUSTOMERS)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.customers < 50:
        raise SystemExit("--customers must be at least 50")

    start_index = next_group03_index(args.output_dir)
    specs = make_specs(args.base_seed, args.customers)

    created = []

    for local_idx, spec in enumerate(specs):
        file_idx = start_index + local_idx

        name = (
            f"{GROUP_PREFIX}_i{file_idx:02d}_"
            f"c{spec.customers}_d{spec.depots}_m{spec.salesmen}_adversarial"
        )

        depots_xy, customers_xy, salesmen_per_depot = GENERATORS[spec.family](spec)

        payload = build_instance_json(
            name=name,
            seed=spec.seed,
            depots_xy=depots_xy,
            customers_xy=customers_xy,
            salesmen_per_depot=salesmen_per_depot,
            family=spec.family,
        )

        output_path = args.output_dir / f"{name}.json"
        created.append((output_path, spec.family, spec.seed))

        if not args.dry_run:
            write_instance(output_path, payload)

    print(f"output_dir: {args.output_dir}")
    print(f"created_instances: {len(created)}")
    print(f"start_index: {start_index:02d}")
    print(f"customers_per_instance: {args.customers}")
    print("families:")

    for family, count in COUNTS.items():
        print(f"  {family}: {count}")

    for path, family, seed in created:
        action = "would write" if args.dry_run else "wrote"
        print(f"  {action}: {path}  family={family} seed={seed}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())