from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any


@dataclass(frozen=True)
class SuiteConfig:
    suite_name: str
    description: str
    algorithms: tuple[str, ...]
    instance_roots: tuple[Path, ...]
    include_globs: tuple[str, ...]
    exclude_globs: tuple[str, ...]
    seeds: tuple[int, ...]
    improve_iterations: int
    return_to_depot: bool | None


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def utc_now_compact() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/mdmtsp"))
    parser.add_argument("--output-root", type=Path, default=Path("results/runs"))
    parser.add_argument("--logs-root", type=Path, default=Path("results/logs"))
    parser.add_argument("--summary-path", type=Path)
    parser.add_argument("--algorithms", nargs="+", help="Override algorithms from config")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def resolve_repo_path(root: Path, value: Path) -> Path:
    if value.is_absolute():
        return value
    return (root / value).resolve()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json_atomic(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(path.name + ".tmp")
    with tmp_path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    tmp_path.replace(path)


def normalize_algorithms(raw_value: Any) -> tuple[str, ...]:
    if isinstance(raw_value, str):
        value = raw_value.strip()
        if not value:
            raise ValueError("algorithm name must be non-empty")
        return (value,)

    if isinstance(raw_value, list):
        values: list[str] = []
        for item in raw_value:
            if not isinstance(item, str) or not item.strip():
                raise ValueError("each algorithm must be a non-empty string")
            values.append(item.strip())
        if not values:
            raise ValueError("field 'algorithms' must not be empty")
        return tuple(values)

    raise ValueError("field 'algorithms' or 'algorithm' has invalid type")


def read_suite_config(config_path: Path, root: Path) -> SuiteConfig:
    data = load_json(config_path)

    if not isinstance(data, dict):
        raise ValueError("suite config must be a JSON object")

    suite_name = data.get("suite_name")
    if not isinstance(suite_name, str) or not suite_name.strip():
        raise ValueError("field 'suite_name' must be a non-empty string")

    description = data.get("description", "")
    if not isinstance(description, str):
        raise ValueError("field 'description' must be a string")

    if "algorithms" in data:
        algorithms = normalize_algorithms(data["algorithms"])
    elif "algorithm" in data:
        algorithms = normalize_algorithms(data["algorithm"])
    else:
        raise ValueError("suite config must contain 'algorithms' or 'algorithm'")

    raw_instance_roots = data.get("instance_roots")
    if not isinstance(raw_instance_roots, list) or not raw_instance_roots:
        raise ValueError("field 'instance_roots' must be a non-empty array")

    instance_roots: list[Path] = []
    for value in raw_instance_roots:
        if not isinstance(value, str) or not value.strip():
            raise ValueError("each entry of 'instance_roots' must be a non-empty string")
        instance_roots.append(resolve_repo_path(root, Path(value)))

    raw_include_globs = data.get("include_globs", ["**/*.json"])
    if not isinstance(raw_include_globs, list) or not raw_include_globs:
        raise ValueError("field 'include_globs' must be a non-empty array")
    include_globs = tuple(str(pattern) for pattern in raw_include_globs)

    raw_exclude_globs = data.get("exclude_globs", [])
    if not isinstance(raw_exclude_globs, list):
        raise ValueError("field 'exclude_globs' must be an array")
    exclude_globs = tuple(str(pattern) for pattern in raw_exclude_globs)

    raw_seeds = data.get("seeds")
    if not isinstance(raw_seeds, list) or not raw_seeds:
        raise ValueError("field 'seeds' must be a non-empty array")
    seeds: list[int] = []
    for value in raw_seeds:
        if not isinstance(value, int) or value < 0:
            raise ValueError("each seed must be a non-negative integer")
        seeds.append(value)

    improve_iterations = data.get("improve_iterations", 0)
    if not isinstance(improve_iterations, int) or improve_iterations < 0:
        raise ValueError("field 'improve_iterations' must be a non-negative integer")

    return_to_depot = data.get("return_to_depot")
    if return_to_depot is not None and not isinstance(return_to_depot, bool):
        raise ValueError("field 'return_to_depot' must be a boolean when provided")

    return SuiteConfig(
        suite_name=suite_name.strip(),
        description=description,
        algorithms=algorithms,
        instance_roots=tuple(instance_roots),
        include_globs=include_globs,
        exclude_globs=exclude_globs,
        seeds=tuple(seeds),
        improve_iterations=improve_iterations,
        return_to_depot=return_to_depot,
    )


def matches_any_glob(
    relative_to_repo: PurePosixPath,
    relative_to_root: PurePosixPath,
    patterns: tuple[str, ...],
) -> bool:
    return any(relative_to_repo.match(pattern) or relative_to_root.match(pattern) for pattern in patterns)


def discover_instances(config: SuiteConfig, root: Path) -> list[Path]:
    discovered: set[Path] = set()

    for instance_root in config.instance_roots:
        if not instance_root.exists():
            raise FileNotFoundError(f"instance root does not exist: {instance_root}")

        if instance_root.is_file():
            candidates = [instance_root]
        else:
            candidates = []
            for pattern in config.include_globs:
                candidates.extend(path for path in instance_root.glob(pattern) if path.is_file())

        for candidate in candidates:
            candidate = candidate.resolve()

            try:
                relative_to_repo = PurePosixPath(candidate.relative_to(root).as_posix())
            except ValueError as exc:
                raise ValueError(f"instance path is outside repository root: {candidate}") from exc

            if instance_root.is_file():
                relative_to_root = PurePosixPath(candidate.name)
            else:
                relative_to_root = PurePosixPath(candidate.relative_to(instance_root).as_posix())

            if config.exclude_globs and matches_any_glob(relative_to_repo, relative_to_root, config.exclude_globs):
                continue

            if candidate.suffix.lower() != ".json":
                continue

            discovered.add(candidate)

    return sorted(discovered)


def instance_output_stem(instance_path: Path, root: Path) -> Path:
    relative = instance_path.relative_to(root).with_suffix("")
    parts = list(relative.parts)
    if parts and parts[0] == "instances":
        parts = parts[1:]
    return Path(*parts) if parts else Path(instance_path.stem)


def build_run_id(suite_name: str, algorithm: str, instance_path: Path, seed: int) -> str:
    stem = instance_path.stem.replace(" ", "_")
    return f"{utc_now_compact()}__{suite_name}__{algorithm}__{stem}__seed_{seed}__{uuid.uuid4().hex[:12]}"


def build_command(
    executable: Path,
    algorithm: str,
    config: SuiteConfig,
    instance_path: Path,
    seed: int,
    output_path: Path,
    run_id: str,
) -> list[str]:
    command = [
        str(executable),
        "--instance",
        str(instance_path),
        "--algorithm",
        algorithm,
        "--seed",
        str(seed),
        "--suite",
        config.suite_name,
        "--run-id",
        run_id,
        "--json",
        "--output",
        str(output_path),
    ]

    if config.improve_iterations > 0:
        command.extend(["--improve-iters", str(config.improve_iterations)])

    if config.return_to_depot is True:
        command.append("--closed")
    elif config.return_to_depot is False:
        command.append("--open")

    return command


def write_log(path: Path, stdout_text: str, stderr_text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    combined = stdout_text
    if stderr_text:
        if combined and not combined.endswith("\n"):
            combined += "\n"
        combined += stderr_text
    path.write_text(combined, encoding="utf-8")


def execute_run(
    *,
    executable: Path,
    algorithm: str,
    config: SuiteConfig,
    instance_path: Path,
    seed: int,
    output_root: Path,
    logs_root: Path,
    root: Path,
    dry_run: bool,
) -> dict[str, Any]:
    run_id = build_run_id(config.suite_name, algorithm, instance_path, seed)
    started_at = utc_now_iso()

    rel_stem = instance_output_stem(instance_path, root)
    output_path = output_root / config.suite_name / algorithm / rel_stem / f"{run_id}.json"
    log_path = logs_root / config.suite_name / algorithm / rel_stem / f"{run_id}.log"

    command = build_command(
        executable=executable,
        algorithm=algorithm,
        config=config,
        instance_path=instance_path,
        seed=seed,
        output_path=output_path,
        run_id=run_id,
    )
    quoted_command = " ".join(shlex.quote(part) for part in command)

    if dry_run:
        return {
            "run_id": run_id,
            "suite_name": config.suite_name,
            "algorithm_id": algorithm,
            "instance_name": instance_path.stem,
            "instance_path": str(instance_path),
            "seed": seed,
            "status": "dry_run",
            "return_code": 0,
            "command": quoted_command,
            "output_path": str(output_path),
            "log_path": str(log_path),
            "started_at_utc": started_at,
            "finished_at_utc": started_at,
        }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    wall_clock_start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    wall_clock_ms = int(round((time.perf_counter() - wall_clock_start) * 1000.0))
    finished_at = utc_now_iso()

    write_log(log_path, completed.stdout, completed.stderr)

    entry: dict[str, Any] = {
        "run_id": run_id,
        "suite_name": config.suite_name,
        "algorithm_id": algorithm,
        "instance_name": instance_path.stem,
        "instance_path": str(instance_path),
        "seed": seed,
        "status": "ok" if completed.returncode == 0 else "failed",
        "return_code": completed.returncode,
        "command": quoted_command,
        "output_path": str(output_path),
        "log_path": str(log_path),
        "started_at_utc": started_at,
        "finished_at_utc": finished_at,
        "runner_wall_clock_ms": wall_clock_ms,
    }

    if completed.returncode != 0:
        return entry

    if not output_path.exists():
        entry["status"] = "failed"
        entry["failure_reason"] = "result file was not created"
        return entry

    try:
        result = load_json(output_path)
    except Exception as exc:
        entry["status"] = "failed"
        entry["failure_reason"] = f"cannot parse result json: {exc}"
        return entry

    entry["objective"] = result.get("objective")
    entry["feasible"] = result.get("feasible")
    entry["solver_status"] = result.get("status")
    entry["solver_wall_time_ms"] = result.get("wall_time_ms")
    entry["result_algorithm_id"] = result.get("algorithm_id")
    entry["result_suite_name"] = result.get("suite_name")

    return entry


def build_summary_payload(
    *,
    config: SuiteConfig,
    selected_algorithms: tuple[str, ...],
    config_path: Path,
    executable: Path,
    entries: list[dict[str, Any]],
    dry_run: bool,
) -> dict[str, Any]:
    ok_runs = sum(1 for entry in entries if entry["status"] == "ok")
    failed_runs = sum(1 for entry in entries if entry["status"] == "failed")
    dry_runs = sum(1 for entry in entries if entry["status"] == "dry_run")

    return {
        "suite_name": config.suite_name,
        "description": config.description,
        "config_path": str(config_path),
        "executable": str(executable),
        "configured_algorithms": list(config.algorithms),
        "selected_algorithms": list(selected_algorithms),
        "generated_at_utc": utc_now_iso(),
        "dry_run": dry_run,
        "total_runs": len(entries),
        "ok_runs": ok_runs,
        "failed_runs": failed_runs,
        "dry_runs": dry_runs,
        "entries": entries,
    }


def main() -> int:
    args = parse_args()
    root = repo_root()

    config_path = resolve_repo_path(root, args.config)
    executable = resolve_repo_path(root, args.executable)
    output_root = resolve_repo_path(root, args.output_root)
    logs_root = resolve_repo_path(root, args.logs_root)

    if not config_path.exists():
        raise FileNotFoundError(f"missing config: {config_path}")
    if not args.dry_run and not executable.exists():
        raise FileNotFoundError(f"missing executable: {executable}")

    config = read_suite_config(config_path, root)

    selected_algorithms = tuple(args.algorithms) if args.algorithms else config.algorithms
    if not selected_algorithms:
        raise RuntimeError("no algorithms selected")

    instances = discover_instances(config, root)
    if not instances:
        raise RuntimeError("no JSON instances matched the suite config")

    entries: list[dict[str, Any]] = []

    for algorithm in selected_algorithms:
        for instance_path in instances:
            for seed in config.seeds:
                entry = execute_run(
                    executable=executable,
                    algorithm=algorithm,
                    config=config,
                    instance_path=instance_path,
                    seed=seed,
                    output_root=output_root,
                    logs_root=logs_root,
                    root=root,
                    dry_run=args.dry_run,
                )
                entries.append(entry)

                line = f"[{entry['status']}] {algorithm} | {instance_path.stem} seed={seed}"
                if entry.get("objective") is not None:
                    line += f" objective={entry['objective']}"
                print(line)

                if args.fail_fast and entry["status"] == "failed":
                    summary_path = args.summary_path
                    if summary_path is None:
                        summary_path = Path("results/run_summaries") / f"{config.suite_name}_{utc_now_compact()}.json"
                    write_json_atomic(
                        resolve_repo_path(root, summary_path),
                        build_summary_payload(
                            config=config,
                            selected_algorithms=selected_algorithms,
                            config_path=config_path,
                            executable=executable,
                            entries=entries,
                            dry_run=args.dry_run,
                        ),
                    )
                    return 1

    summary_path = args.summary_path
    if summary_path is None:
        summary_path = Path("results/run_summaries") / f"{config.suite_name}_{utc_now_compact()}.json"

    write_json_atomic(
        resolve_repo_path(root, summary_path),
        build_summary_payload(
            config=config,
            selected_algorithms=selected_algorithms,
            config_path=config_path,
            executable=executable,
            entries=entries,
            dry_run=args.dry_run,
        ),
    )

    return 1 if any(entry["status"] == "failed" for entry in entries) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)