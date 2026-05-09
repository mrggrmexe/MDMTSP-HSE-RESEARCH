#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


GROUP_PREFIX = "g03t"
DEFAULT_OUTPUT_DIR = Path("instances/research/group_03_traps")
DEFAULT_CUSTOMERS = 750

COUNTS = {
    "comb_trap": 7,
    "ring_trap": 6,
    "alternating_clusters": 5,
    "satellite_outliers": 4,
    "depot_conflict": 3,
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

    out: list[tuple[float, float]] = []
    for x, y in points:
        nx = (x - min_x) * scale + width * 0.04
        ny = (y - min_y) * scale + height * 0.04
        out.append((round(clamp(nx, 0.0, width), 6), round(clamp(ny, 0.0, height), 6)))
    return out


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
    depots = []
    customers = []

    for depot_id, ((x, y), salesmen) in enumerate(zip(depots_xy, salesmen_per_depot, strict=True)):
        depots.append(
            {
                "id": depot_id,
                "x": x,
                "y": y,
                "salesmen": int(salesmen),
            }
        )

    next_id = len(depots_xy)
    for idx, (x, y) in enumerate(customers_xy):
        customers.append(
            {
                "id": next_id + idx,
                "x": x,
                "y": y,
            }
        )

    return {
        "name": name,
        "type": "euclidean",
        "seed": int(seed),
        "distribution": "trap_adversarial",
        "trap_family": family,
        "depots": depots,
        "customers": customers,
    }


def gen_comb_trap(spec: InstanceSpec) -> tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]:
    rng = random.Random(spec.seed)

    customers: list[tuple[float, float]] = []

    spine_y_top = 35.0
    spine_y_bottom = -35.0
    left_x = -180.0
    right_x = 180.0

    tooth_count = 14
    points_per_tooth = spec.customers // tooth_count
    remaining = spec.customers - tooth_count * points_per_tooth

    xs = [left_x + i * (right_x - left_x) / (tooth_count - 1) for i in range(tooth_count)]

    for i, x in enumerate(xs):
        n = points_per_tooth + (1 if i < remaining else 0)
        top = (i % 2 == 0)
        base_y = spine_y_top if top else spine_y_bottom
        tip_y = 170.0 if top else -170.0

        for _ in range(n):
            if rng.random() < 0.45:
                customers.append(sample_segment_point(rng, x, base_y, x, tip_y, 3.8))
            else:
                customers.append(sample_gaussian_point(rng, x, base_y, 8.0))

    depots = [
        (-190.0, 0.0),
        (190.0, 0.0),
        (0.0, 205.0),
        (0.0, -205.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.35, 1.35, 0.9, 0.9])

    depots = normalize_points(depots)
    customers = normalize_points(customers)
    return depots, customers, salesmen


def gen_ring_trap(spec: InstanceSpec) -> tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]:
    rng = random.Random(spec.seed)

    customers: list[tuple[float, float]] = []
    outer_n = int(spec.customers * 0.62)
    inner_n = int(spec.customers * 0.22)
    chord_n = spec.customers - outer_n - inner_n

    for _ in range(outer_n):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        radius = rng.uniform(180.0, 220.0)
        customers.append((radius * math.cos(angle), radius * math.sin(angle)))

    for _ in range(inner_n):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        radius = rng.uniform(90.0, 125.0)
        customers.append((radius * math.cos(angle), radius * math.sin(angle)))

    for _ in range(chord_n):
        a = rng.uniform(0.0, 2.0 * math.pi)
        b = a + rng.uniform(0.9, 1.8)
        p1 = (210.0 * math.cos(a), 210.0 * math.sin(a))
        p2 = (210.0 * math.cos(b), 210.0 * math.sin(b))
        customers.append(sample_segment_point(rng, p1[0], p1[1], p2[0], p2[1], 4.0))

    depots = [
        (0.0, 0.0),
        (-235.0, 25.0),
        (235.0, -25.0),
        (0.0, 250.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.0, 1.2, 1.2, 0.8])

    depots = normalize_points(depots)
    customers = normalize_points(customers)
    return depots, customers, salesmen


def gen_alternating_clusters(spec: InstanceSpec) -> tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]:
    rng = random.Random(spec.seed)

    customers: list[tuple[float, float]] = []
    cluster_count = 10
    per_cluster = spec.customers // cluster_count
    remainder = spec.customers - cluster_count * per_cluster

    centers = []
    for i in range(cluster_count):
        x = -200.0 + i * 45.0
        y = 65.0 if i % 2 == 0 else -65.0
        centers.append((x, y))

    for i, (cx, cy) in enumerate(centers):
        n = per_cluster + (1 if i < remainder else 0)
        sigma = 10.0 if i % 2 == 0 else 16.0
        for _ in range(n):
            customers.append(sample_gaussian_point(rng, cx, cy, sigma))

    depots = [
        (-220.0, 0.0),
        (220.0, 0.0),
        (0.0, 125.0),
        (0.0, -125.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.25, 1.25, 0.9, 0.9])

    depots = normalize_points(depots)
    customers = normalize_points(customers)
    return depots, customers, salesmen


def gen_satellite_outliers(spec: InstanceSpec) -> tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]:
    rng = random.Random(spec.seed)

    customers: list[tuple[float, float]] = []
    core_n = int(spec.customers * 0.68)
    satellite_n = int(spec.customers * 0.20)
    bridge_n = spec.customers - core_n - satellite_n

    for _ in range(core_n):
        customers.append(sample_gaussian_point(rng, 0.0, 0.0, 26.0))

    satellite_centers = [
        (-230.0, -170.0),
        (-240.0, 170.0),
        (240.0, -160.0),
        (225.0, 175.0),
    ]

    per_sat = satellite_n // len(satellite_centers)
    rem = satellite_n - per_sat * len(satellite_centers)

    for i, (cx, cy) in enumerate(satellite_centers):
        n = per_sat + (1 if i < rem else 0)
        for _ in range(n):
            customers.append(sample_gaussian_point(rng, cx, cy, 12.0))

    for _ in range(bridge_n):
        customers.append(sample_disc_point(rng, 0.0, 0.0, 145.0))

    depots = [
        (-35.0, -35.0),
        (35.0, 35.0),
        (-265.0, 0.0),
        (265.0, 0.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.0, 1.0, 1.15, 1.15])

    depots = normalize_points(depots)
    customers = normalize_points(customers)
    return depots, customers, salesmen


def gen_depot_conflict(spec: InstanceSpec) -> tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]:
    rng = random.Random(spec.seed)

    customers: list[tuple[float, float]] = []

    zone_a = int(spec.customers * 0.28)
    zone_b = int(spec.customers * 0.28)
    center_conflict = int(spec.customers * 0.28)
    border_conflict = spec.customers - zone_a - zone_b - center_conflict

    for _ in range(zone_a):
        customers.append(sample_gaussian_point(rng, -120.0, 0.0, 24.0))
    for _ in range(zone_b):
        customers.append(sample_gaussian_point(rng, 120.0, 0.0, 24.0))
    for _ in range(center_conflict):
        customers.append(sample_gaussian_point(rng, 0.0, 0.0, 32.0))
    for _ in range(border_conflict):
        x = rng.uniform(-65.0, 65.0)
        y = rng.uniform(-170.0, 170.0)
        customers.append((x + rng.gauss(0.0, 4.0), y + rng.gauss(0.0, 4.0)))

    depots = [
        (-150.0, 0.0),
        (150.0, 0.0),
        (0.0, 160.0),
        (0.0, -160.0),
    ][: spec.depots]

    salesmen = allocate_salesmen(rng, spec.depots, spec.salesmen, [1.0, 1.0, 0.95, 0.95])

    depots = normalize_points(depots)
    customers = normalize_points(customers)
    return depots, customers, salesmen


GENERATORS: dict[str, Callable[[InstanceSpec], tuple[list[tuple[float, float]], list[tuple[float, float]], list[int]]]] = {
    "comb_trap": gen_comb_trap,
    "ring_trap": gen_ring_trap,
    "alternating_clusters": gen_alternating_clusters,
    "satellite_outliers": gen_satellite_outliers,
    "depot_conflict": gen_depot_conflict,
}


def make_specs(base_seed: int, customers: int) -> list[InstanceSpec]:
    variants = {
        "comb_trap": [(4, 5), (3, 4), (2, 3)],
        "ring_trap": [(4, 5), (3, 4), (2, 3)],
        "alternating_clusters": [(4, 5), (3, 4), (2, 3)],
        "satellite_outliers": [(4, 5), (3, 4)],
        "depot_conflict": [(4, 5), (3, 4), (2, 3)],
    }

    specs: list[InstanceSpec] = []
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
    parser = argparse.ArgumentParser(
        description="Generate 25 trap-style MDMTSP instances for instances/research/group_03_traps"
    )
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--base-seed", type=int, default=206000)
    parser.add_argument("--customers", type=int, default=DEFAULT_CUSTOMERS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    if args.customers < 100:
        raise SystemExit("--customers must be at least 100")

    specs = make_specs(args.base_seed, args.customers)

    if len(specs) != 25:
        raise RuntimeError(f"expected 25 specs, got {len(specs)}")

    created = []

    for idx, spec in enumerate(specs, start=1):
        name = (
            f"{GROUP_PREFIX}_i{idx:02d}_"
            f"c{spec.customers}_d{spec.depots}_m{spec.salesmen}_trap"
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
        print(f"  {action}: {path} family={family} d={depots} m={salesmen} seed={seed}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())