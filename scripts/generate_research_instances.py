from __future__ import annotations

import argparse
import json
import random
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


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
class InstanceSpec:
    group_id: int
    instance_count: int
    customers: int
    depots_min: int
    depots_max: int


DEFAULT_SPECS: tuple[InstanceSpec, ...] = (
    InstanceSpec(1, 10, 10, 1, 3),
    InstanceSpec(2, 10, 50, 3, 5),
    InstanceSpec(3, 10, 500, 3, 5),
    InstanceSpec(4, 5, 1000, 5, 10),
    InstanceSpec(5, 5, 10000, 1, 3),
    InstanceSpec(6, 5, 10000, 5, 10),
    InstanceSpec(7, 2, 25000, 5, 10),
    InstanceSpec(8, 2, 100000, 1, 3),
    InstanceSpec(9, 2, 100000, 5, 10),
    InstanceSpec(10, 2, 100000, 100, 1000),
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


def random_point(rng: random.Random, width: float, height: float) -> Point:
    return Point(
        x=rng.uniform(0.0, width),
        y=rng.uniform(0.0, height),
    )


def choose_depot_count(rng: random.Random, spec: InstanceSpec) -> int:
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


def build_instance_payload(
    *,
    instance_name: str,
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

    depots: list[Depot] = []
    customers: list[Customer] = []

    next_node_id = 0

    for depot_index in range(depot_count):
        point = random_point(rng, width, height)
        depots.append(
            Depot(
                id=next_node_id,
                x=point.x,
                y=point.y,
                salesmen=salesmen_per_depot[depot_index],
            )
        )
        next_node_id += 1

    for _ in range(customer_count):
        point = random_point(rng, width, height)
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
        "seed": seed,
        "return_to_depot": return_to_depot,
        "depots": [asdict(depot) for depot in depots],
        "customers": [asdict(customer) for customer in customers],
    }


def file_name_for_spec(spec: InstanceSpec, local_index: int, depot_count: int, total_salesmen: int) -> str:
    return (
        f"g{spec.group_id:02d}"
        f"_i{local_index:02d}"
        f"_c{spec.customers}"
        f"_d{depot_count}"
        f"_m{total_salesmen}"
        ".json"
    )


def instance_name_for_spec(spec: InstanceSpec, local_index: int, depot_count: int, total_salesmen: int) -> str:
    return (
        f"group_{spec.group_id:02d}"
        f"_instance_{local_index:02d}"
        f"_customers_{spec.customers}"
        f"_depots_{depot_count}"
        f"_salesmen_{total_salesmen}"
    )


def total_salesmen_from_payload(payload: dict) -> int:
    return sum(int(depot["salesmen"]) for depot in payload["depots"])


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

            temporary_name = "temporary"
            payload = build_instance_payload(
                instance_name=temporary_name,
                seed=instance_seed,
                depot_count=depot_count,
                customer_count=spec.customers,
                width=width,
                height=height,
                return_to_depot=return_to_depot,
            )

            total_salesmen = total_salesmen_from_payload(payload)
            instance_name = instance_name_for_spec(
                spec,
                local_index,
                depot_count,
                total_salesmen,
            )
            file_name = file_name_for_spec(
                spec,
                local_index,
                depot_count,
                total_salesmen,
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