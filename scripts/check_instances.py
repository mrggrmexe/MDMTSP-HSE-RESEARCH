#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Iterable


_FILENAME_PATTERNS = [
    re.compile(r"(?:^|_)c(?P<customers>\d+)_d(?P<depots>\d+)_m(?P<salesmen>\d+)(?:_|$)"),
    re.compile(
        r"(?:^|_)customers_(?P<customers>\d+)_depots_(?P<depots>\d+)_salesmen_(?P<salesmen>\d+)(?:_|$)"
    ),
]


class ValidationError(Exception):
    pass


@dataclass
class Point:
    x: float
    y: float


@dataclass
class FileReport:
    path: str
    status: str
    schema: str | None = None
    name: str | None = None
    distance_type: str | None = None
    return_to_depot: bool | None = None
    seed: int | None = None
    depot_count: int = 0
    customer_count: int = 0
    salesmen_count: int = 0
    node_count: int = 0
    bbox_min_x: float | None = None
    bbox_min_y: float | None = None
    bbox_max_x: float | None = None
    bbox_max_y: float | None = None
    warnings: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def add_warning(self, message: str) -> None:
        self.warnings.append(message)

    def add_error(self, message: str) -> None:
        self.errors.append(message)

    def to_row(self) -> dict[str, Any]:
        return {
            "path": self.path,
            "status": self.status,
            "schema": self.schema or "",
            "name": self.name or "",
            "distance_type": self.distance_type or "",
            "return_to_depot": "" if self.return_to_depot is None else str(self.return_to_depot),
            "seed": "" if self.seed is None else self.seed,
            "depot_count": self.depot_count,
            "customer_count": self.customer_count,
            "salesmen_count": self.salesmen_count,
            "node_count": self.node_count,
            "bbox_min_x": "" if self.bbox_min_x is None else self.bbox_min_x,
            "bbox_min_y": "" if self.bbox_min_y is None else self.bbox_min_y,
            "bbox_max_x": "" if self.bbox_max_x is None else self.bbox_max_x,
            "bbox_max_y": "" if self.bbox_max_y is None else self.bbox_max_y,
            "warnings_count": len(self.warnings),
            "errors_count": len(self.errors),
            "warnings": " | ".join(self.warnings),
            "errors": " | ".join(self.errors),
        }


@dataclass
class Options:
    fail_on_warning: bool = False
    filename_check: bool = True


class _SchemaProbe:
    def __init__(self, name: str, seed: int | None, return_to_depot: bool | None, distance_type: str | None,
                 depots: list[dict[str, Any]], customers: list[dict[str, Any]], points: list[Point]) -> None:
        self.name = name
        self.seed = seed
        self.return_to_depot = return_to_depot
        self.distance_type = distance_type
        self.depots = depots
        self.customers = customers
        self.points = points



def _is_non_negative_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0



def _read_non_negative_int(obj: dict[str, Any], key: str, context: str) -> int:
    if key not in obj:
        raise ValidationError(f"{context}.{key} is missing")
    value = obj[key]
    if not _is_non_negative_int(value):
        raise ValidationError(f"{context}.{key} must be a non-negative integer")
    return int(value)



def _read_optional_non_negative_int(obj: dict[str, Any], key: str, context: str) -> int | None:
    if key not in obj:
        return None
    value = obj[key]
    if not _is_non_negative_int(value):
        raise ValidationError(f"{context}.{key} must be a non-negative integer")
    return int(value)



def _read_bool(obj: dict[str, Any], key: str, context: str) -> bool:
    if key not in obj:
        raise ValidationError(f"{context}.{key} is missing")
    value = obj[key]
    if not isinstance(value, bool):
        raise ValidationError(f"{context}.{key} must be a boolean")
    return value



def _read_optional_bool(obj: dict[str, Any], key: str, context: str) -> bool | None:
    if key not in obj:
        return None
    value = obj[key]
    if not isinstance(value, bool):
        raise ValidationError(f"{context}.{key} must be a boolean")
    return value



def _read_string(obj: dict[str, Any], key: str, context: str) -> str:
    if key not in obj:
        raise ValidationError(f"{context}.{key} is missing")
    value = obj[key]
    if not isinstance(value, str):
        raise ValidationError(f"{context}.{key} must be a string")
    return value



def _read_optional_string(obj: dict[str, Any], key: str, context: str) -> str | None:
    if key not in obj:
        return None
    value = obj[key]
    if not isinstance(value, str):
        raise ValidationError(f"{context}.{key} must be a string")
    return value



def _read_finite_float(obj: dict[str, Any], key: str, context: str) -> float:
    if key not in obj:
        raise ValidationError(f"{context}.{key} is missing")
    value = obj[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValidationError(f"{context}.{key} must be a finite number")
    number = float(value)
    if not math.isfinite(number):
        raise ValidationError(f"{context}.{key} must be finite")
    return number



def _read_point(obj: dict[str, Any], context: str) -> Point:
    return Point(
        x=_read_finite_float(obj, "x", context),
        y=_read_finite_float(obj, "y", context),
    )



def _looks_like_new_schema(root: dict[str, Any]) -> bool:
    depots = root.get("depots")
    if not isinstance(depots, list) or not depots:
        return False
    first = depots[0]
    if not isinstance(first, dict):
        return False
    return (
        "id" in first
        or "salesmen" in first
        or "type" in root
        or "seed" in root
    )



def _validate_root_structure(root: Any) -> dict[str, Any]:
    if not isinstance(root, dict):
        raise ValidationError("root must be a JSON object")
    depots = root.get("depots")
    customers = root.get("customers")
    if not isinstance(depots, list):
        raise ValidationError("root.depots must be an array")
    if not isinstance(customers, list):
        raise ValidationError("root.customers must be an array")
    if not depots:
        raise ValidationError("root.depots must not be empty")
    if not customers:
        raise ValidationError("root.customers must not be empty")
    return root



def _probe_new_schema(root: dict[str, Any]) -> _SchemaProbe:
    name = root.get("name") if isinstance(root.get("name"), str) else ""
    seed = _read_optional_non_negative_int(root, "seed", "root")
    return_to_depot = _read_optional_bool(root, "return_to_depot", "root")
    distance_type = _read_optional_string(root, "type", "root")
    if distance_type is not None and distance_type not in {"euclidean", "euclidean2d", "euc_2d"}:
        raise ValidationError(f"root.type has unsupported value: {distance_type}")

    depots_raw = root["depots"]
    customers_raw = root["customers"]
    used_ids: set[int] = set()
    depots: list[dict[str, Any]] = []
    customers: list[dict[str, Any]] = []
    points: list[Point] = []

    for index, item in enumerate(depots_raw):
        context = f"depots[{index}]"
        if not isinstance(item, dict):
            raise ValidationError(f"{context} must be an object")
        depot_id = _read_non_negative_int(item, "id", context)
        if depot_id in used_ids:
            raise ValidationError(f"duplicate node id in {context}: {depot_id}")
        used_ids.add(depot_id)
        point = _read_point(item, context)
        salesmen = _read_non_negative_int(item, "salesmen", context)
        depots.append({"id": depot_id, "salesmen": salesmen, "point": point})
        points.append(point)

    for index, item in enumerate(customers_raw):
        context = f"customers[{index}]"
        if not isinstance(item, dict):
            raise ValidationError(f"{context} must be an object")
        customer_id = _read_non_negative_int(item, "id", context)
        if customer_id in used_ids:
            raise ValidationError(f"duplicate node id in {context}: {customer_id}")
        used_ids.add(customer_id)
        point = _read_point(item, context)
        customers.append({"id": customer_id, "point": point})
        points.append(point)

    return _SchemaProbe(name, seed, return_to_depot, distance_type, depots, customers, points)



def _probe_legacy_schema(root: dict[str, Any]) -> _SchemaProbe:
    name = root.get("name") if isinstance(root.get("name"), str) else ""
    seed = _read_optional_non_negative_int(root, "seed", "root")
    return_to_depot = _read_optional_bool(root, "return_to_depot", "root")
    salesman_count = _read_non_negative_int(root, "salesman_count", "root")

    depots_raw = root["depots"]
    customers_raw = root["customers"]
    depots: list[dict[str, Any]] = []
    customers: list[dict[str, Any]] = []
    points: list[Point] = []

    depot_count = len(depots_raw)
    base = salesman_count // depot_count
    remainder = salesman_count % depot_count

    for index, item in enumerate(depots_raw):
        context = f"depots[{index}]"
        if not isinstance(item, dict):
            raise ValidationError(f"{context} must be an object")
        point = _read_point(item, context)
        salesmen = base + (1 if index < remainder else 0)
        depots.append({"id": index, "salesmen": salesmen, "point": point})
        points.append(point)

    for index, item in enumerate(customers_raw):
        context = f"customers[{index}]"
        if not isinstance(item, dict):
            raise ValidationError(f"{context} must be an object")
        point = _read_point(item, context)
        customers.append({"id": depot_count + index, "point": point})
        points.append(point)

    return _SchemaProbe(name, seed, return_to_depot, "euclidean", depots, customers, points)



def _infer_filename_metadata(path: Path) -> tuple[int, int, int] | None:
    stem = path.stem
    for pattern in _FILENAME_PATTERNS:
        match = pattern.search(stem)
        if match:
            return (
                int(match.group("customers")),
                int(match.group("depots")),
                int(match.group("salesmen")),
            )
    return None



def _bbox(points: list[Point]) -> tuple[float, float, float, float] | None:
    if not points:
        return None
    xs = [p.x for p in points]
    ys = [p.y for p in points]
    return min(xs), min(ys), max(xs), max(ys)



def validate_instance_file(path: Path, options: Options | None = None) -> FileReport:
    options = options or Options()
    report = FileReport(path=str(path), status="ok")

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        root = _validate_root_structure(data)
        schema = "new" if _looks_like_new_schema(root) else "legacy"
        report.schema = schema
        probe = _probe_new_schema(root) if schema == "new" else _probe_legacy_schema(root)

        report.name = probe.name
        report.seed = probe.seed
        report.return_to_depot = probe.return_to_depot
        report.distance_type = probe.distance_type
        report.depot_count = len(probe.depots)
        report.customer_count = len(probe.customers)
        report.salesmen_count = sum(int(d["salesmen"]) for d in probe.depots)
        report.node_count = report.depot_count + report.customer_count
        bbox = _bbox(probe.points)
        if bbox is not None:
            report.bbox_min_x, report.bbox_min_y, report.bbox_max_x, report.bbox_max_y = bbox

        if report.salesmen_count <= 0:
            report.add_error("total number of salesmen must be positive")

        if schema == "new":
            zero_salesmen_depots = [str(d["id"]) for d in probe.depots if int(d["salesmen"]) == 0]
            if zero_salesmen_depots:
                report.add_warning(
                    "depots with zero salesmen: " + ", ".join(zero_salesmen_depots)
                )

        if probe.name == "":
            report.add_warning("name is missing or empty; filename stem will likely be used instead")

        if report.customer_count > 0 and report.salesmen_count > report.customer_count:
            report.add_warning(
                "salesmen_count is greater than customer_count; this is unusual for the project datasets"
            )

        if options.filename_check:
            metadata = _infer_filename_metadata(path)
            if metadata is not None:
                exp_customers, exp_depots, exp_salesmen = metadata
                if exp_customers != report.customer_count:
                    report.add_warning(
                        f"filename customers={exp_customers} but file contains {report.customer_count} customers"
                    )
                if exp_depots != report.depot_count:
                    report.add_warning(
                        f"filename depots={exp_depots} but file contains {report.depot_count} depots"
                    )
                if exp_salesmen != report.salesmen_count:
                    report.add_warning(
                        f"filename salesmen={exp_salesmen} but file contains {report.salesmen_count} salesmen"
                    )

        if report.bbox_min_x == report.bbox_max_x and report.bbox_min_y == report.bbox_max_y:
            report.add_warning("all points collapse to a single coordinate")

        if report.errors:
            report.status = "error"
        elif report.warnings:
            report.status = "warning"
    except json.JSONDecodeError as exc:
        report.status = "error"
        report.add_error(f"invalid JSON: {exc}")
    except OSError as exc:
        report.status = "error"
        report.add_error(str(exc))
    except ValidationError as exc:
        report.status = "error"
        report.add_error(str(exc))

    return report



def discover_instance_files(paths: Iterable[Path], glob_pattern: str) -> list[Path]:
    files: list[Path] = []
    seen: set[Path] = set()
    for input_path in paths:
        path = input_path.expanduser()
        if path.is_file():
            candidate = path.resolve()
            if candidate not in seen:
                seen.add(candidate)
                files.append(candidate)
            continue
        if path.is_dir():
            for candidate in sorted(path.rglob(glob_pattern)):
                if candidate.is_file():
                    resolved = candidate.resolve()
                    if resolved not in seen:
                        seen.add(resolved)
                        files.append(resolved)
    return files



def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="check_instances.py",
        description="Validate MDMTSP instance JSON files in both new and legacy repository schemas.",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["instances"],
        help="Instance files or directories to scan recursively (default: instances).",
    )
    parser.add_argument(
        "--glob",
        default="*.json",
        help="Glob pattern for recursive directory scan (default: *.json).",
    )
    parser.add_argument(
        "--report-jsonl",
        type=Path,
        help="Optional path for a JSONL report with one record per checked file.",
    )
    parser.add_argument(
        "--report-csv",
        type=Path,
        help="Optional path for a CSV report with one row per checked file.",
    )
    parser.add_argument(
        "--fail-on-warning",
        action="store_true",
        help="Return a non-zero exit code when warnings are present.",
    )
    parser.add_argument(
        "--no-filename-check",
        action="store_true",
        help="Disable filename metadata consistency checks (c/d/m patterns).",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Only print the final summary.",
    )
    return parser



def _write_jsonl(path: Path, reports: list[FileReport]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for report in reports:
            handle.write(json.dumps(asdict(report), ensure_ascii=False) + "\n")



def _write_csv(path: Path, reports: list[FileReport]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = [report.to_row() for report in reports]
    fieldnames = [
        "path",
        "status",
        "schema",
        "name",
        "distance_type",
        "return_to_depot",
        "seed",
        "depot_count",
        "customer_count",
        "salesmen_count",
        "node_count",
        "bbox_min_x",
        "bbox_min_y",
        "bbox_max_x",
        "bbox_max_y",
        "warnings_count",
        "errors_count",
        "warnings",
        "errors",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)



def _print_report(report: FileReport) -> None:
    prefix = {
        "ok": "OK",
        "warning": "WARN",
        "error": "ERROR",
    }.get(report.status, report.status.upper())
    print(f"[{prefix}] {report.path}")
    if report.schema is not None:
        print(
            f"       schema={report.schema} depots={report.depot_count} customers={report.customer_count} "
            f"salesmen={report.salesmen_count}"
        )
    for warning in report.warnings:
        print(f"       warning: {warning}")
    for error in report.errors:
        print(f"       error: {error}")



def _print_summary(reports: list[FileReport]) -> None:
    total = len(reports)
    ok_count = sum(1 for report in reports if report.status == "ok")
    warning_count = sum(1 for report in reports if report.status == "warning")
    error_count = sum(1 for report in reports if report.status == "error")
    print(
        f"checked={total} ok={ok_count} warnings={warning_count} errors={error_count}"
    )



def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    roots = [Path(item) for item in args.paths]
    files = discover_instance_files(roots, args.glob)
    if not files:
        print("check_instances.py: no matching instance files found", file=sys.stderr)
        return 1

    options = Options(
        fail_on_warning=bool(args.fail_on_warning),
        filename_check=not bool(args.no_filename_check),
    )
    reports = [validate_instance_file(path, options) for path in files]

    if not args.quiet:
        for report in reports:
            _print_report(report)
    _print_summary(reports)

    if args.report_jsonl is not None:
        _write_jsonl(args.report_jsonl, reports)
    if args.report_csv is not None:
        _write_csv(args.report_csv, reports)

    has_errors = any(report.status == "error" for report in reports)
    has_warnings = any(report.status == "warning" for report in reports)
    if has_errors:
        return 1
    if has_warnings and args.fail_on_warning:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
