#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


GROUP_PREFIX = "g03h"
DEFAULT_OUTPUT_DIR = Path("instances/research/group_03_hard")
DEFAULT_CUSTOMERS = 500

COUNTS = {
    "overlapping_cluster": 7,
    "bridge_line": 5,
    "asymmetric_depot": 5,
    "mixed_density": 4,
    "outlier_heavy": 4,
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
            round(clamp((x - min_x) * scale + width * 0.04, 0.0, width), 6),
            round(clamp((y - min_y) * scale + height * 0.04, 0.0, height), 6),
        )
        for x, y in points
    ]


def allocate_salesmen(rng: random.Random, depots: int, salesmen: int, bias: list[float]) -> list[int]:
    if salesmen < depots:
        raise ValueError("salesmen must be >= depots")

    if len(bias) < depots:
        bias = bias + [1.0] * (depots - len(bias))

    weights = bias[:depots]
    total = sum(weights)
    probs = [w / total for w in weights]

    alloc = [1] * depots
    extra = salesmen - depots

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
            "x": x,
            "y": y,
            "salesmen": int(salesmen),
        }
        for depot_id, ((x, y), salesmen) in enumerate(zip(depots_xy, salesmen_per_depot, strict=True))
    ]

    customers = [
        {
            "id": len(depots_xy) + idx,
            "x": x,
            "y": y,
        }
        for idx, (x, y) in enumerate(customers_xy)
    ]

    return {
        "name": name,
        "type": "euclidean",
        "seed": int(seed),
        "distribution": "hard_adversarial",
        "adversarial_family": family,
        "depots": depots,
        "customers": customers,
    }


def gen_overlapping_cluster(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    centers = [(0.0, 0.0), (95.0, 15.0), (45.0, 75.0)]
    sigmas = [34.0, 36.0, 32.0]
    weights = [0.42, 0.35, 0.23]

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

        if rng.random() < 0.18:
            mix = (cluster + rng.randint(1, 2)) % 3
            x2, y2 = sample_gaussian_point(rng, *centers[mix], sigmas[mix])
            x = 0.50 * x + 0.50 * x2
            y = 0.50 * y + 0.50 * y2

        customers.append((x, y))

    depots = [(-35.0, -20.0), (120.0, 0.0), (45.0, 105.0)][: spec.depots]
    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.1, 1.1, 0.9])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_bridge_line(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []
    left_n = int(spec.customers * 0.36)
    right_n = int(spec.customers * 0.36)
    bridge_n = int(spec.customers * 0.20)
    diagonal_n = spec.customers - left_n - right_n - bridge_n

    for _ in range(left_n):
        customers.append(sample_gaussian_point(rng, -150.0, 0.0, 24.0))

    for _ in range(right_n):
        customers.append(sample_gaussian_point(rng, 150.0, 0.0, 24.0))

    for _ in range(bridge_n):
        customers.append(sample_segment_point(rng, -115.0, -5.0, 115.0, 5.0, 5.5))

    for _ in range(diagonal_n):
        customers.append(sample_segment_point(rng, -190.0, 65.0, 190.0, -65.0, 4.5))

    depots = [(-185.0, -60.0), (185.0, 60.0), (0.0, 95.0)][: spec.depots]
    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.45, 1.45, 0.7])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_asymmetric_depot(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    centers = [(-70.0, -10.0), (20.0, 35.0), (110.0, -5.0), (40.0, -85.0)]
    weights = [0.52, 0.22, 0.16, 0.10]

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

        sigma = 20.0 if cluster == 0 else 14.0
        customers.append(sample_gaussian_point(rng, *centers[cluster], sigma))

    depots = [(-135.0, -105.0), (145.0, 95.0), (130.0, -95.0)][: spec.depots]
    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [2.8, 0.8, 0.7])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_mixed_density(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []
    dense_n = int(spec.customers * 0.58)
    medium_n = int(spec.customers * 0.25)
    sparse_n = spec.customers - dense_n - medium_n

    for _ in range(dense_n):
        customers.append(sample_gaussian_point(rng, -35.0, 20.0, 7.0))

    for _ in range(medium_n):
        customers.append(sample_gaussian_point(rng, 90.0, -15.0, 30.0))

    for _ in range(sparse_n):
        customers.append((rng.uniform(-170.0, 170.0), rng.uniform(-130.0, 130.0)))

    depots = [(-80.0, -55.0), (120.0, 25.0), (10.0, 110.0)][: spec.depots]
    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.5, 1.0, 0.8])

    return normalize_points(depots), normalize_points(customers), salesmen


def gen_outlier_heavy(spec: InstanceSpec):
    rng = random.Random(spec.seed)

    customers = []
    core_n = int(spec.customers * 0.68)
    near_outlier_n = int(spec.customers * 0.14)
    far_outlier_n = spec.customers - core_n - near_outlier_n

    for _ in range(core_n):
        customers.append(sample_gaussian_point(rng, 0.0, 0.0, 21.0))

    for _ in range(near_outlier_n):
        customers.append(sample_disc_point(rng, 0.0, 0.0, 155.0))

    for _ in range(far_outlier_n):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        radius = rng.uniform(240.0, 330.0)
        customers.append((radius * math.cos(angle), radius * math.sin(angle)))

    depots = [(-45.0, -35.0), (45.0, 35.0), (0.0, 115.0)][: spec.depots]
    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.1, 1.1, 0.8])

    return normalize_points(depots), normalize_points(customers), salesmen


GENERATORS: dict[str, Callable] = {
    "overlapping_cluster": gen_overlapping_cluster,
    "bridge_line": gen_bridge_line,
    "asymmetric_depot": gen_asymmetric_depot,
    "mixed_density": gen_mixed_density,
    "outlier_heavy": gen_outlier_heavy,
}


def make_specs(base_seed: int, customers: int) -> list[InstanceSpec]:
    variants = {
        "overlapping_cluster": [(3, 5), (3, 6), (2, 4)],
        "bridge_line": [(2, 4), (3, 5)],
        "asymmetric_depot": [(3, 5), (2, 4)],
        "mixed_density": [(3, 6), (2, 5)],
        "outlier_heavy": [(2, 4), (3, 5)],
    }

    specs = []
    offset = 0

    for family, count in COUNTS.items():
        family_variants = variants[family]

        for i in range(count):
            depots, salesmen = family_variants[i % len(family_variants)]
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
    parser = argparse.ArgumentParser(description="Generate 25 hard adversarial MDMTSP instances for group_03_hard")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--base-seed", type=int, default=205000)
    parser.add_argument("--customers", type=int, default=DEFAULT_CUSTOMERS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    if args.customers < 50:
        raise SystemExit("--customers must be at least 50")

    specs = make_specs(args.base_seed, args.customers)

    if len(specs) != 25:
        raise RuntimeError(f"expected 25 specs, got {len(specs)}")

    created = []

    for idx, spec in enumerate(specs, start=1):
        name = (
            f"{GROUP_PREFIX}_i{idx:02d}_"
            f"c{spec.customers}_d{spec.depots}_m{spec.salesmen}_hard_adversarial"
        )

        output_path = args.output_dir / f"{name}.json"

        if output_path.exists() and not args.overwrite:
            raise SystemExit(f"file already exists: {output_path}; use --overwrite")

        depots_xy, customers_xy, salesmen_per_depot = GENERATORS[spec.family](spec)

        payload = build_instance_json(
            name=name,
            seed=spec.seed,
            depots_xy=depots_xy,
            customers_xy=customers_xy,
            salesmen_per_depot=salesmen_per_depot,
            family=spec.family,
        )

        created.append((output_path, spec.family, spec.depots, spec.salesmen, spec.seed))

        if not args.dry_run:
            write_instance(output_path, payload)

    print(f"output_dir: {args.output_dir}")
    print(f"created_instances: {len(created)}")
    print(f"customers_per_instance: {args.customers}")
    print("families:")
    for family, count in COUNTS.items():
        print(f"  {family}: {count}")

    for path, family, depots, salesmen, seed in created:
        action = "would write" if args.dry_run else "wrote"
        print(f"  {action}: {path}  family={family} d={depots} m={salesmen} seed={seed}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())