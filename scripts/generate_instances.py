from __future__ import annotations

import argparse
import json
import random
import sys
from dataclasses import dataclass, asdict
from pathlib import Path


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Instance:
    name: str
    return_to_depot: bool
    salesman_count: int
    depots: list[Point]
    customers: list[Point]


def random_point(rng: random.Random, width: float, height: float) -> Point:
    return Point(
        x=rng.uniform(0.0, width),
        y=rng.uniform(0.0, height),
    )


def make_instance(
    name: str,
    seed: int,
    depot_count: int,
    customer_count: int,
    salesman_count: int,
    width: float,
    height: float,
    return_to_depot: bool,
) -> Instance:
    rng = random.Random(seed)

    depots = [random_point(rng, width, height) for _ in range(depot_count)]
    customers = [random_point(rng, width, height) for _ in range(customer_count)]

    return Instance(
        name=name,
        return_to_depot=return_to_depot,
        salesman_count=salesman_count,
        depots=depots,
        customers=customers,
    )


def to_json_dict(instance: Instance) -> dict:
    return {
        "name": instance.name,
        "return_to_depot": instance.return_to_depot,
        "salesman_count": instance.salesman_count,
        "depots": [asdict(point) for point in instance.depots],
        "customers": [asdict(point) for point in instance.customers],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--seed", required=True, type=int)
    parser.add_argument("--depots", required=True, type=int)
    parser.add_argument("--customers", required=True, type=int)
    parser.add_argument("--salesmen", required=True, type=int)
    parser.add_argument("--width", required=True, type=float)
    parser.add_argument("--height", required=True, type=float)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--open", action="store_true")
    parser.add_argument("--closed", action="store_true")
    args = parser.parse_args()

    if args.depots <= 0 or args.customers <= 0 or args.salesmen <= 0:
        print("depots, customers and salesmen must be positive", file=sys.stderr)
        return 1

    if args.width <= 0.0 or args.height <= 0.0:
        print("width and height must be positive", file=sys.stderr)
        return 1

    if args.open and args.closed:
        print("use only one of --open or --closed", file=sys.stderr)
        return 1

    return_to_depot = True
    if args.open:
        return_to_depot = False

    instance = make_instance(
        name=args.name,
        seed=args.seed,
        depot_count=args.depots,
        customer_count=args.customers,
        salesman_count=args.salesmen,
        width=args.width,
        height=args.height,
        return_to_depot=return_to_depot,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(to_json_dict(instance), ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())