from __future__ import annotations

import argparse
import json
import math
import random
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Depot:
    id: int
    x: float
    y: float
    salesmen: int


@dataclass(frozen=True)
class Customer:
    id: int
    x: float
    y: float


@dataclass(frozen=True)
class GroupSpec:
    group_id: int
    instance_count: int
    customers: int
    depots_min: int
    depots_max: int
    special_types: tuple[str, ...] = ()


DEFAULT_SPECS: tuple[GroupSpec, ...] = (
    GroupSpec(1, 10, 10, 1, 3),
    GroupSpec(2, 10, 50, 3, 5),
    GroupSpec(3, 10, 500, 3, 5),
    GroupSpec(4, 5, 1000, 5, 10),
    GroupSpec(5, 5, 10000, 1, 3),
    GroupSpec(
        6,
        8,
        10000,
        5,
        10,
        special_types=(
            "clustered",
            "clustered",
            "grid",
            "grid",
            "adversarial",
            "adversarial",
            "line",
            "line",
        ),
    ),
    GroupSpec(7, 2, 25000, 5, 10),
    GroupSpec(8, 2, 100000, 1, 3),
    GroupSpec(9, 2, 100000, 5, 10),
    GroupSpec(10, 2, 100000, 100, 1000),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=Path("instances/research"))
    parser.add_argument("--master-seed", type=int, default=20260504)
    parser.add_argument("--width", type=float, default=1000.0)
    parser.add_argument("--height", type=float, default=1000.0)
    parser.add_argument("--open-routes", action="store_true")
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args()


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def random_point(rng: random.Random, width: float, height: float) -> Point:
    return Point(
        x=rng.uniform(0.0, width),
        y=rng.uniform(0.0, height),
    )


def choose_depot_count(rng: random.Random, spec: GroupSpec) -> int:
    return rng.randint(spec.depots_min, spec.depots_max)


def choose_total_salesmen(
    rng: random.Random,
    customer_count: int,
    depot_count: int,
) -> int:
    if customer_count <= 20:
        low = depot_count
        high = min(customer_count, depot_count * 3)
    elif customer_count <= 1000:
        low = depot_count
        high = min(customer_count, depot_count * 5)
    elif customer_count <= 25000:
        low = depot_count * 2
        high = min(customer_count, depot_count * 8)
    else:
        low = depot_count * 3
        high = min(customer_count, depot_count * 12)

    low = max(1, min(low, customer_count))
    high = max(low, min(high, customer_count))
    return rng.randint(low, high)


def distribute_salesmen_across_depots(
    rng: random.Random,
    depot_count: int,
    total_salesmen: int,
) -> list[int]:
    if depot_count <= 0:
        raise ValueError("depot_count must be positive")
    if total_salesmen < depot_count:
        raise ValueError("total_salesmen must be at least depot_count")

    salesmen = [1] * depot_count
    remaining = total_salesmen - depot_count

    for _ in range(remaining):
        salesmen[rng.randrange(depot_count)] += 1

    return salesmen


def generate_random_depots(
    rng: random.Random,
    depot_count: int,
    width: float,
    height: float,
) -> list[Point]:
    return [random_point(rng, width, height) for _ in range(depot_count)]


def generate_random_customers(
    rng: random.Random,
    customer_count: int,
    width: float,
    height: float,
) -> list[Point]:
    return [random_point(rng, width, height) for _ in range(customer_count)]


def generate_clustered_customers(
    rng: random.Random,
    customer_count: int,
    width: float,
    height: float,
) -> list[Point]:
    cluster_count = rng.randint(8, 16)
    centers = [random_point(rng, width, height) for _ in range(cluster_count)]
    sigma_x = width / rng.uniform(20.0, 35.0)
    sigma_y = height / rng.uniform(20.0, 35.0)

    customers: list[Point] = []
    for _ in range(customer_count):
        center = centers[rng.randrange(cluster_count)]
        x = clamp(rng.gauss(center.x, sigma_x), 0.0, width)
        y = clamp(rng.gauss(center.y, sigma_y), 0.0, height)
        customers.append(Point(x=x, y=y))

    return customers


def generate_grid_customers(
    rng: random.Random,
    customer_count: int,
    width: float,
    height: float,
) -> list[Point]:
    cols = max(2, int(math.sqrt(customer_count)))
    rows = max(2, math.ceil(customer_count / cols))

    step_x = width / max(1, cols - 1)
    step_y = height / max(1, rows - 1)
    jitter_x = step_x * 0.15
    jitter_y = step_y * 0.15

    customers: list[Point] = []
    for row in range(rows):
        for col in range(cols):
            if len(customers) >= customer_count:
                break
            base_x = col * step_x
            base_y = row * step_y
            x = clamp(base_x + rng.uniform(-jitter_x, jitter_x), 0.0, width)
            y = clamp(base_y + rng.uniform(-jitter_y, jitter_y), 0.0, height)
            customers.append(Point(x=x, y=y))

    return customers


def generate_adversarial_depots(
    rng: random.Random,
    depot_count: int,
    width: float,
    height: float,
) -> list[Point]:
    band_width = width * 0.08
    return [
        Point(
            x=rng.uniform(0.0, band_width),
            y=rng.uniform(0.0, height),
        )
        for _ in range(depot_count)
    ]


def generate_adversarial_customers(
    rng: random.Random,
    customer_count: int,
    width: float,
    height: float,
) -> list[Point]:
    cluster_count = rng.randint(6, 12)
    centers: list[Point] = []

    for _ in range(cluster_count):
        centers.append(
            Point(
                x=rng.uniform(width * 0.55, width),
                y=rng.uniform(0.0, height),
            )
        )

    sigma_x = width / rng.uniform(30.0, 50.0)
    sigma_y = height / rng.uniform(20.0, 35.0)

    customers: list[Point] = []
    for _ in range(customer_count):
        center = centers[rng.randrange(cluster_count)]
        x = clamp(rng.gauss(center.x, sigma_x), 0.0, width)
        y = clamp(rng.gauss(center.y, sigma_y), 0.0, height)
        customers.append(Point(x=x, y=y))

    return customers


def generate_line_customers(
    rng: random.Random,
    customer_count: int,
    width: float,
    height: float,
) -> list[Point]:
    line_type = rng.choice(("diagonal", "horizontal", "vertical"))
    noise = min(width, height) / rng.uniform(80.0, 140.0)

    customers: list[Point] = []

    for _ in range(customer_count):
        t = rng.uniform(0.0, 1.0)

        if line_type == "diagonal":
            base_x = t * width
            base_y = t * height
            x = clamp(rng.gauss(base_x, noise), 0.0, width)
            y = clamp(rng.gauss(base_y, noise), 0.0, height)
        elif line_type == "horizontal":
            base_x = t * width
            base_y = height * 0.5
            x = clamp(rng.gauss(base_x, noise), 0.0, width)
            y = clamp(rng.gauss(base_y, noise), 0.0, height)
        else:
            base_x = width * 0.5
            base_y = t * height
            x = clamp(rng.gauss(base_x, noise), 0.0, width)
            y = clamp(rng.gauss(base_y, noise), 0.0, height)

        customers.append(Point(x=x, y=y))

    return customers


def generate_points_for_instance(
    *,
    rng: random.Random,
    instance_type: str,
    depot_count: int,
    customer_count: int,
    width: float,
    height: float,
) -> tuple[list[Point], list[Point]]:
    if instance_type == "random":
        depots = generate_random_depots(rng, depot_count, width, height)
        customers = generate_random_customers(rng, customer_count, width, height)
        return depots, customers

    if instance_type == "clustered":
        depots = generate_random_depots(rng, depot_count, width, height)
        customers = generate_clustered_customers(rng, customer_count, width, height)
        return depots, customers

    if instance_type == "grid":
        depots = generate_random_depots(rng, depot_count, width, height)
        customers = generate_grid_customers(rng, customer_count, width, height)
        return depots, customers

    if instance_type == "adversarial":
        depots = generate_adversarial_depots(rng, depot_count, width, height)
        customers = generate_adversarial_customers(rng, customer_count, width, height)
        return depots, customers

    if instance_type == "line":
        depots = generate_random_depots(rng, depot_count, width, height)
        customers = generate_line_customers(rng, customer_count, width, height)
        return depots, customers

    raise ValueError(f"unsupported instance_type: {instance_type}")


def build_instance_payload(
    *,
    instance_name: str,
    instance_type: str,
    seed: int,
    depot_count: int,
    customer_count: int,
    width: float,
    height: float,
    return_to_depot: bool,
) -> dict:
    rng = random.Random(seed)

    total_salesmen = choose_total_salesmen(
        rng=rng,
        customer_count=customer_count,
        depot_count=depot_count,
    )
    salesmen_per_depot = distribute_salesmen_across_depots(
        rng=rng,
        depot_count=depot_count,
        total_salesmen=total_salesmen,
    )

    depot_points, customer_points = generate_points_for_instance(
        rng=rng,
        instance_type=instance_type,
        depot_count=depot_count,
        customer_count=customer_count,
        width=width,
        height=height,
    )

    depots: list[Depot] = []
    customers: list[Customer] = []

    next_node_id = 0

    for depot_index, point in enumerate(depot_points):
        depots.append(
            Depot(
                id=next_node_id,
                x=point.x,
                y=point.y,
                salesmen=salesmen_per_depot[depot_index],
            )
        )
        next_node_id += 1

    for point in customer_points:
        customers.append(
            Customer(
                id=next_node_id,
                x=point.x,
                y=point.y,
            )
        )
        next_node_id += 1

    return {
        "name": instance_name,
        "type": "euclidean",
        "instance_type": instance_type,
        "seed": seed,
        "return_to_depot": return_to_depot,
        "depots": [asdict(depot) for depot in depots],
        "customers": [asdict(customer) for customer in customers],
    }


def total_salesmen_from_payload(payload: dict) -> int:
    return sum(int(depot["salesmen"]) for depot in payload["depots"])


def file_name_for_instance(
    spec: GroupSpec,
    local_index: int,
    depot_count: int,
    total_salesmen: int,
    instance_type: str,
) -> str:
    return (
        f"g{spec.group_id:02d}"
        f"_i{local_index:02d}"
        f"_c{spec.customers}"
        f"_d{depot_count}"
        f"_m{total_salesmen}"
        f"_{instance_type}"
        ".json"
    )


def instance_name_for_instance(
    spec: GroupSpec,
    local_index: int,
    depot_count: int,
    total_salesmen: int,
    instance_type: str,
) -> str:
    return (
        f"group_{spec.group_id:02d}"
        f"_instance_{local_index:02d}"
        f"_customers_{spec.customers}"
        f"_depots_{depot_count}"
        f"_salesmen_{total_salesmen}"
        f"_{instance_type}"
    )


def write_json(path: Path, payload: dict, pretty: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8") as handle:
        if pretty:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        else:
            json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))


def generate_all_instances(
    *,
    output_dir: Path,
    master_seed: int,
    width: float,
    height: float,
    return_to_depot: bool,
    pretty: bool,
) -> list[Path]:
    if width <= 0.0 or height <= 0.0:
        raise ValueError("width and height must be positive")

    master_rng = random.Random(master_seed)
    written_files: list[Path] = []

    for spec in DEFAULT_SPECS:
        group_dir = output_dir / f"group_{spec.group_id:02d}"

        for local_index in range(1, spec.instance_count + 1):
            depot_count = choose_depot_count(master_rng, spec)
            instance_seed = master_rng.randint(0, 2**31 - 1)

            if spec.special_types:
                instance_type = spec.special_types[local_index - 1]
            else:
                instance_type = "random"

            payload = build_instance_payload(
                instance_name="temporary",
                instance_type=instance_type,
                seed=instance_seed,
                depot_count=depot_count,
                customer_count=spec.customers,
                width=width,
                height=height,
                return_to_depot=return_to_depot,
            )

            total_salesmen = total_salesmen_from_payload(payload)
            instance_name = instance_name_for_instance(
                spec=spec,
                local_index=local_index,
                depot_count=depot_count,
                total_salesmen=total_salesmen,
                instance_type=instance_type,
            )
            file_name = file_name_for_instance(
                spec=spec,
                local_index=local_index,
                depot_count=depot_count,
                total_salesmen=total_salesmen,
                instance_type=instance_type,
            )

            payload["name"] = instance_name

            output_path = group_dir / file_name
            write_json(output_path, payload, pretty)
            written_files.append(output_path)

    return written_files


def main() -> int:
    args = parse_args()
    return_to_depot = not args.open_routes

    try:
        written_files = generate_all_instances(
            output_dir=args.output_dir,
            master_seed=args.master_seed,
            width=args.width,
            height=args.height,
            return_to_depot=return_to_depot,
            pretty=args.pretty,
        )
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"generated files: {len(written_files)}")
    print(f"output dir: {args.output_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())