from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import LineCollection
from matplotlib.gridspec import GridSpec
from matplotlib.lines import Line2D


@dataclass(frozen=True)
class NodeRecord:
    node_id: int
    x: float
    y: float
    kind: str
    depot_id: int | None = None


@dataclass(frozen=True)
class RouteRecord:
    route_index: int
    depot_id: int | None
    nodes: tuple[int, ...]


@dataclass(frozen=True)
class InstanceData:
    name: str
    return_to_depot: bool
    salesman_count: int | None
    depots: tuple[NodeRecord, ...]
    customers: tuple[NodeRecord, ...]

    @property
    def depot_count(self) -> int:
        return len(self.depots)

    @property
    def customer_count(self) -> int:
        return len(self.customers)


@dataclass(frozen=True)
class RunData:
    source_path: Path
    instance_path: Path | None
    algorithm_id: str | None
    objective: float | None
    feasible: bool | None
    wall_time_ms: float | None
    routes: tuple[RouteRecord, ...]
    raw: dict[str, Any]


@dataclass(frozen=True)
class VisualizationOptions:
    output_path: Path
    dpi: int
    figure_width: float
    figure_height: float
    annotate_depots: bool
    show_legend: bool
    equal_aspect: bool
    route_color_mode: str
    line_width: float | None
    customer_size: float | None
    depot_size: float | None
    title: str | None
    transparent: bool
    pad_fraction: float


class VisualizeSolutionError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise VisualizeSolutionError(f"file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise VisualizeSolutionError(f"invalid JSON in {path}: {exc}") from exc


def _as_float(value: Any, *, field_name: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise VisualizeSolutionError(f"field '{field_name}' must be numeric") from exc


def _as_int(value: Any, *, field_name: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError) as exc:
        raise VisualizeSolutionError(f"field '{field_name}' must be an integer") from exc


def _normalize_point_array(items: Sequence[Any], *, kind: str, start_id: int) -> tuple[NodeRecord, ...]:
    records: list[NodeRecord] = []
    for offset, item in enumerate(items):
        if not isinstance(item, dict):
            raise VisualizeSolutionError(f"{kind}[{offset}] must be an object")

        node_id = _as_int(item.get("id", start_id + offset), field_name=f"{kind}[{offset}].id")
        x = _as_float(item.get("x"), field_name=f"{kind}[{offset}].x")
        y = _as_float(item.get("y"), field_name=f"{kind}[{offset}].y")
        depot_id = None
        if kind == "depot":
            depot_id = node_id
        records.append(NodeRecord(node_id=node_id, x=x, y=y, kind=kind, depot_id=depot_id))
    return tuple(records)


def _normalize_simple_point_array(items: Sequence[Any], *, kind: str, start_id: int) -> tuple[NodeRecord, ...]:
    records: list[NodeRecord] = []
    for offset, item in enumerate(items):
        if not isinstance(item, dict):
            raise VisualizeSolutionError(f"{kind}s[{offset}] must be an object with x/y fields")
        x = _as_float(item.get("x"), field_name=f"{kind}s[{offset}].x")
        y = _as_float(item.get("y"), field_name=f"{kind}s[{offset}].y")
        node_id = start_id + offset
        depot_id = node_id if kind == "depot" else None
        records.append(NodeRecord(node_id=node_id, x=x, y=y, kind=kind, depot_id=depot_id))
    return tuple(records)


def load_instance(path: Path) -> InstanceData:
    payload = load_json(path)

    depots_raw = payload.get("depots")
    customers_raw = payload.get("customers")
    if not isinstance(depots_raw, list) or not isinstance(customers_raw, list):
        raise VisualizeSolutionError("instance JSON must contain list fields 'depots' and 'customers'")

    has_explicit_ids = any(isinstance(item, dict) and "id" in item for item in depots_raw + customers_raw)
    if has_explicit_ids:
        depots = _normalize_point_array(depots_raw, kind="depot", start_id=0)
        customers = _normalize_point_array(customers_raw, kind="customer", start_id=len(depots))
    else:
        depots = _normalize_simple_point_array(depots_raw, kind="depot", start_id=0)
        customers = _normalize_simple_point_array(customers_raw, kind="customer", start_id=len(depots))

    node_ids = [node.node_id for node in (*depots, *customers)]
    if len(node_ids) != len(set(node_ids)):
        raise VisualizeSolutionError(f"instance file contains duplicate node ids: {path}")

    return_to_depot = bool(payload.get("return_to_depot", True))
    salesman_count = payload.get("salesman_count")
    if salesman_count is None and depots_raw and isinstance(depots_raw[0], dict) and "salesmen" in depots_raw[0]:
        try:
            salesman_count = sum(_as_int(item.get("salesmen", 0), field_name="depots[].salesmen") for item in depots_raw)
        except VisualizeSolutionError:
            salesman_count = None
    if salesman_count is not None:
        salesman_count = _as_int(salesman_count, field_name="salesman_count")

    return InstanceData(
        name=str(payload.get("name", path.stem)),
        return_to_depot=return_to_depot,
        salesman_count=salesman_count,
        depots=depots,
        customers=customers,
    )


def load_run(path: Path) -> RunData:
    payload = load_json(path)

    routes_raw = payload.get("routes")
    if not isinstance(routes_raw, list):
        raise VisualizeSolutionError("run/solution JSON must contain list field 'routes'")

    routes: list[RouteRecord] = []
    for index, item in enumerate(routes_raw):
        if not isinstance(item, dict):
            raise VisualizeSolutionError(f"routes[{index}] must be an object")
        nodes_raw = item.get("nodes")
        if not isinstance(nodes_raw, list):
            raise VisualizeSolutionError(f"routes[{index}].nodes must be a list")
        depot_id = item.get("depot_id")
        if depot_id is not None:
            depot_id = _as_int(depot_id, field_name=f"routes[{index}].depot_id")
        routes.append(
            RouteRecord(
                route_index=index,
                depot_id=depot_id,
                nodes=tuple(_as_int(node, field_name=f"routes[{index}].nodes[]") for node in nodes_raw),
            )
        )

    instance_path: Path | None = None
    for candidate in (payload.get("instance_path"), payload.get("instance", {}).get("path") if isinstance(payload.get("instance"), dict) else None):
        if candidate:
            instance_path = Path(str(candidate)).expanduser()
            break

    execution = payload.get("execution") if isinstance(payload.get("execution"), dict) else {}
    wall_time_ms = None
    if isinstance(execution, dict) and execution.get("wall_time_ms") is not None:
        wall_time_ms = _as_float(execution.get("wall_time_ms"), field_name="execution.wall_time_ms")

    feasible = payload.get("feasible")
    if feasible is None and isinstance(payload.get("result"), dict):
        feasible = payload["result"].get("feasible")
    if feasible is not None:
        feasible = bool(feasible)

    objective = payload.get("objective")
    if objective is None and isinstance(payload.get("result"), dict):
        objective = payload["result"].get("objective")
    if objective is not None:
        objective = _as_float(objective, field_name="objective")

    algorithm_id = payload.get("algorithm_id")
    if algorithm_id is None and isinstance(payload.get("algorithm"), dict):
        algorithm_id = payload["algorithm"].get("id")
    if algorithm_id is not None:
        algorithm_id = str(algorithm_id)

    return RunData(
        source_path=path,
        instance_path=instance_path,
        algorithm_id=algorithm_id,
        objective=objective,
        feasible=feasible,
        wall_time_ms=wall_time_ms,
        routes=tuple(routes),
        raw=payload,
    )


def default_output_path(input_path: Path) -> Path:
    base = input_path.with_suffix("")
    return base.parent / f"{base.name}__solution.png"


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Visualize an MDMTSP solution/run JSON with fast rendering for large instances."
    )
    parser.add_argument("input", type=Path, help="Path to run.json or solution JSON")
    parser.add_argument(
        "--instance",
        type=Path,
        default=None,
        help="Optional explicit path to instance JSON. Overrides instance_path embedded in the run JSON.",
    )
    parser.add_argument("--output", type=Path, default=None, help="Output image path. Default: <input>__solution.png")
    parser.add_argument("--dpi", type=int, default=180, help="Output DPI for raster formats")
    parser.add_argument("--figure-width", type=float, default=14.0)
    parser.add_argument("--figure-height", type=float, default=8.0)
    parser.add_argument("--title", type=str, default=None, help="Optional custom title")
    parser.add_argument("--annotate-depots", action="store_true", help="Annotate depot ids on the plot")
    parser.add_argument("--no-legend", action="store_true", help="Disable legend")
    parser.add_argument("--no-equal-aspect", action="store_true", help="Do not force equal aspect ratio")
    parser.add_argument(
        "--route-color-mode",
        choices=("auto", "route", "depot", "mono"),
        default="auto",
        help="Color routes individually, by depot, monochrome, or choose automatically.",
    )
    parser.add_argument("--line-width", type=float, default=None, help="Override route line width")
    parser.add_argument("--customer-size", type=float, default=None, help="Override customer marker size")
    parser.add_argument("--depot-size", type=float, default=None, help="Override depot marker size")
    parser.add_argument("--transparent", action="store_true", help="Save figure with transparent background")
    parser.add_argument("--pad-fraction", type=float, default=0.04, help="Relative plot padding around geometry")
    return parser.parse_args(argv)


def build_node_lookup(instance: InstanceData) -> dict[int, NodeRecord]:
    lookup = {node.node_id: node for node in (*instance.depots, *instance.customers)}
    if len(lookup) != instance.depot_count + instance.customer_count:
        raise VisualizeSolutionError("duplicate node ids detected after instance normalization")
    return lookup


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _resolve_candidate_path(path: Path, repo_root: Path) -> Path | None:
    expanded = path.expanduser()
    if expanded.exists():
        return expanded.resolve()

    parts = expanded.parts
    if "instances" in parts:
        idx = parts.index("instances")
        candidate = repo_root.joinpath(*parts[idx:])
        if candidate.exists():
            return candidate.resolve()

    basename = expanded.name
    if basename:
        matches = list((repo_root / "instances").rglob(basename))
        if len(matches) == 1:
            return matches[0].resolve()

    return None


def resolve_instance_path(run: RunData, override: Path | None) -> Path:
    repo_root = _repo_root()
    if override is not None:
        resolved = _resolve_candidate_path(override, repo_root)
        if resolved is None:
            raise VisualizeSolutionError(f"file not found: {override}")
        return resolved
    if run.instance_path is None:
        raise VisualizeSolutionError(
            "instance path is missing in the run JSON; pass --instance explicitly"
        )
    resolved = _resolve_candidate_path(run.instance_path, repo_root)
    if resolved is None:
        raise VisualizeSolutionError(
            f"file not found: {run.instance_path}"
        )
    return resolved


def _estimate_marker_size(point_count: int, *, is_depot: bool) -> float:
    if is_depot:
        if point_count <= 10:
            return 90.0
        if point_count <= 50:
            return 70.0
        return 52.0

    if point_count <= 100:
        return 18.0
    if point_count <= 500:
        return 10.0
    if point_count <= 2_000:
        return 5.0
    if point_count <= 10_000:
        return 2.0
    if point_count <= 50_000:
        return 0.8
    return 0.35


def _estimate_line_width(route_count: int, customer_count: int) -> float:
    if customer_count <= 100:
        return 1.8
    if customer_count <= 1_000:
        return 1.2
    if customer_count <= 10_000:
        return 0.8 if route_count <= 50 else 0.65
    return 0.45


def _choose_color_mode(requested: str, depot_count: int, route_count: int) -> str:
    if requested != "auto":
        return requested
    if route_count <= 24:
        return "route"
    if depot_count <= 20:
        return "depot"
    return "mono"


def _make_palette(count: int, cmap_name: str) -> list[Any]:
    if count <= 0:
        return []
    cmap = plt.get_cmap(cmap_name)
    if count == 1:
        return [cmap(0.15)]
    return [cmap(i / max(1, count - 1)) for i in range(count)]


def build_route_segments(
    routes: Sequence[RouteRecord],
    node_lookup: dict[int, NodeRecord],
) -> tuple[list[np.ndarray], list[int], list[int]]:
    segments: list[np.ndarray] = []
    route_sizes: list[int] = []
    depot_ids: list[int] = []

    for route in routes:
        if not route.nodes:
            continue
        coords: list[tuple[float, float]] = []
        customer_visits = 0
        for node_id in route.nodes:
            node = node_lookup.get(node_id)
            if node is None:
                raise VisualizeSolutionError(f"route references unknown node id {node_id}")
            coords.append((node.x, node.y))
            if node.kind == "customer":
                customer_visits += 1

        if len(coords) >= 2:
            segments.append(np.asarray(coords, dtype=np.float64))
            route_sizes.append(customer_visits)
            depot_ids.append(route.depot_id if route.depot_id is not None else -1)

    return segments, route_sizes, depot_ids


def _route_lengths(segments: Iterable[np.ndarray]) -> list[float]:
    lengths: list[float] = []
    for segment in segments:
        if len(segment) < 2:
            lengths.append(0.0)
            continue
        deltas = np.diff(segment, axis=0)
        lengths.append(float(np.linalg.norm(deltas, axis=1).sum()))
    return lengths


def _data_bounds(instance: InstanceData) -> tuple[float, float, float, float]:
    xs = np.array([node.x for node in (*instance.depots, *instance.customers)], dtype=np.float64)
    ys = np.array([node.y for node in (*instance.depots, *instance.customers)], dtype=np.float64)
    if xs.size == 0 or ys.size == 0:
        raise VisualizeSolutionError("instance contains no geometry to visualize")
    return float(xs.min()), float(xs.max()), float(ys.min()), float(ys.max())


def _apply_bounds(ax: plt.Axes, instance: InstanceData, pad_fraction: float) -> None:
    xmin, xmax, ymin, ymax = _data_bounds(instance)
    xspan = xmax - xmin
    yspan = ymax - ymin
    span = max(xspan, yspan, 1.0)
    pad = span * max(0.0, pad_fraction)
    ax.set_xlim(xmin - pad, xmax + pad)
    ax.set_ylim(ymin - pad, ymax + pad)


def _format_summary_lines(
    run: RunData,
    instance: InstanceData,
    route_sizes: Sequence[int],
    route_lengths: Sequence[float],
) -> list[str]:
    lines = [
        f"Instance: {instance.name}",
        f"Customers: {instance.customer_count:,}",
        f"Depots: {instance.depot_count:,}",
        f"Routes shown: {len(route_sizes):,}",
        f"Return to depot: {'yes' if instance.return_to_depot else 'no'}",
    ]
    if instance.salesman_count is not None:
        lines.append(f"Salesmen: {instance.salesman_count:,}")
    if run.algorithm_id:
        lines.append(f"Algorithm: {run.algorithm_id}")
    if run.objective is not None:
        lines.append(f"Objective: {run.objective:,.6f}")
    if run.feasible is not None:
        lines.append(f"Feasible: {'yes' if run.feasible else 'no'}")
    if run.wall_time_ms is not None:
        lines.append(f"Wall time: {run.wall_time_ms:,.3f} ms")

    if route_sizes:
        lines.append("")
        lines.append("Route customer counts:")
        lines.append(
            f"min / median / max = {min(route_sizes):,} / {statistics.median(route_sizes):,.1f} / {max(route_sizes):,}"
        )
    if route_lengths:
        lines.append("Approx. Euclidean route length:")
        lines.append(
            f"min / median / max = {min(route_lengths):,.1f} / {statistics.median(route_lengths):,.1f} / {max(route_lengths):,.1f}"
        )
    return lines


def _set_spine_style(ax: plt.Axes) -> None:
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def render_solution(
    run: RunData,
    instance: InstanceData,
    options: VisualizationOptions,
) -> None:
    node_lookup = build_node_lookup(instance)
    route_segments, route_sizes, depot_ids = build_route_segments(run.routes, node_lookup)
    route_lengths = _route_lengths(route_segments)

    route_count = len(route_segments)
    customer_count = instance.customer_count
    depot_count = instance.depot_count

    color_mode = _choose_color_mode(options.route_color_mode, depot_count=depot_count, route_count=route_count)
    if color_mode == "route":
        segment_colors = _make_palette(max(route_count, 1), "tab20")
    elif color_mode == "depot":
        palette = _make_palette(max(depot_count, 1), "tab10" if depot_count <= 10 else "tab20")
        segment_colors = [palette[depot_id % len(palette)] if depot_id >= 0 else (0.3, 0.3, 0.3, 0.85) for depot_id in depot_ids]
    else:
        segment_colors = [(0.2, 0.4, 0.75, 0.78)] * max(route_count, 1)

    line_width = options.line_width if options.line_width is not None else _estimate_line_width(route_count, customer_count)
    customer_size = options.customer_size if options.customer_size is not None else _estimate_marker_size(customer_count, is_depot=False)
    depot_size = options.depot_size if options.depot_size is not None else _estimate_marker_size(depot_count, is_depot=True)

    figure = plt.figure(figsize=(options.figure_width, options.figure_height), constrained_layout=True)
    gs = GridSpec(1, 2, figure=figure, width_ratios=[4.8, 1.6])
    ax_map = figure.add_subplot(gs[0, 0])
    ax_side = figure.add_subplot(gs[0, 1])

    customer_xy = np.asarray([(node.x, node.y) for node in instance.customers], dtype=np.float64)
    depot_xy = np.asarray([(node.x, node.y) for node in instance.depots], dtype=np.float64)

    if customer_xy.size > 0:
        customer_scatter = ax_map.scatter(
            customer_xy[:, 0],
            customer_xy[:, 1],
            s=customer_size,
            c=np.full((len(customer_xy), 4), (0.28, 0.28, 0.28, 0.70)),
            linewidths=0.0,
            marker="o",
            rasterized=True,
            zorder=1,
        )
        customer_scatter.set_rasterized(True)

    if route_segments:
        collection = LineCollection(
            route_segments,
            colors=segment_colors[:route_count],
            linewidths=line_width,
            alpha=0.85,
            antialiaseds=False,
            rasterized=True,
            zorder=2,
        )
        collection.set_rasterized(True)
        ax_map.add_collection(collection)

    if depot_xy.size > 0:
        depot_colors = _make_palette(max(depot_count, 1), "tab10" if depot_count <= 10 else "tab20")
        depot_scatter = ax_map.scatter(
            depot_xy[:, 0],
            depot_xy[:, 1],
            s=depot_size,
            c=depot_colors[:depot_count],
            linewidths=0.9,
            edgecolors="black",
            marker="s",
            zorder=3,
        )
        depot_scatter.set_rasterized(False)

    if options.annotate_depots:
        for depot in instance.depots:
            ax_map.annotate(
                f"D{depot.node_id}",
                xy=(depot.x, depot.y),
                xytext=(5, 4),
                textcoords="offset points",
                fontsize=9,
                weight="bold",
                zorder=4,
            )

    ax_map.set_xlabel("x")
    ax_map.set_ylabel("y")
    _set_spine_style(ax_map)
    ax_map.grid(True, linewidth=0.4, alpha=0.25)
    _apply_bounds(ax_map, instance, options.pad_fraction)
    if options.equal_aspect:
        ax_map.set_aspect("equal", adjustable="box")

    title = options.title
    if title is None:
        prefix = run.algorithm_id or "solution"
        title = f"{prefix} — {instance.name}"
    ax_map.set_title(title)

    ax_side.set_axis_off()
    summary_text = "\n".join(_format_summary_lines(run, instance, route_sizes, route_lengths))
    ax_side.text(
        0.0,
        1.0,
        summary_text,
        transform=ax_side.transAxes,
        va="top",
        ha="left",
        fontsize=9.5,
        family="monospace",
    )

    inset = ax_side.inset_axes([0.06, 0.05, 0.9, 0.27])
    if route_sizes:
        bins = min(20, max(5, int(math.sqrt(len(route_sizes)))))
        inset.hist(route_sizes, bins=bins)
        inset.set_title("Customers per route", fontsize=9)
        inset.set_xlabel("count", fontsize=8)
        inset.set_ylabel("freq", fontsize=8)
        inset.tick_params(labelsize=8)
        _set_spine_style(inset)
    else:
        inset.set_axis_off()

    if options.show_legend:
        handles: list[Line2D] = [
            Line2D([0], [0], marker="o", color="none", markerfacecolor=(0.28, 0.28, 0.28, 0.70), markeredgewidth=0, markersize=6, label="customers"),
            Line2D([0], [0], marker="s", color="black", markerfacecolor=(0.8, 0.2, 0.2, 1.0), markersize=7, label="depots"),
        ]
        if color_mode == "route" and route_count <= 12:
            for idx, color in enumerate(segment_colors[:route_count]):
                handles.append(Line2D([0], [0], color=color, lw=2.0, label=f"route {idx}"))
        elif color_mode == "depot" and depot_count <= 12:
            palette = _make_palette(max(depot_count, 1), "tab10" if depot_count <= 10 else "tab20")
            for depot in instance.depots[:depot_count]:
                handles.append(Line2D([0], [0], color=palette[depot.node_id % len(palette)], lw=2.0, label=f"depot {depot.node_id}"))
        ax_map.legend(handles=handles, loc="upper right", fontsize=8, frameon=True)

    options.output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(options.output_path, dpi=options.dpi, transparent=options.transparent)
    plt.close(figure)


def make_options(args: argparse.Namespace, input_path: Path) -> VisualizationOptions:
    output_path = args.output if args.output is not None else default_output_path(input_path)
    return VisualizationOptions(
        output_path=output_path,
        dpi=args.dpi,
        figure_width=args.figure_width,
        figure_height=args.figure_height,
        annotate_depots=bool(args.annotate_depots),
        show_legend=not bool(args.no_legend),
        equal_aspect=not bool(args.no_equal_aspect),
        route_color_mode=str(args.route_color_mode),
        line_width=args.line_width,
        customer_size=args.customer_size,
        depot_size=args.depot_size,
        title=args.title,
        transparent=bool(args.transparent),
        pad_fraction=float(args.pad_fraction),
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    input_path = args.input.expanduser().resolve()
    options = make_options(args, input_path)

    try:
        run = load_run(input_path)
        instance_path = resolve_instance_path(run, args.instance)
        instance = load_instance(instance_path)
        render_solution(run, instance, options)
    except VisualizeSolutionError as exc:
        print(f"visualize_solution.py: {exc}", file=sys.stderr)
        return 1

    print(options.output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
