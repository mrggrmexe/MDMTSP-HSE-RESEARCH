from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class RunSpec:
    instance_name: str
    seed: int
    depots: int
    customers: int
    salesmen: int
    width: float
    height: float
    return_to_depot: bool
    improve_iterations: int
    output_json: bool
    output_path: str


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def merge_run(global_config: dict[str, Any], run_config: dict[str, Any]) -> RunSpec:
    merged = dict(global_config)
    merged.update(run_config)

    required = {
        "instance_name",
        "seed",
        "depots",
        "customers",
        "salesmen",
        "width",
        "height",
        "return_to_depot",
        "improve_iterations",
        "output_json",
    }
    missing = sorted(required - set(merged))
    if missing:
        raise ValueError(f"missing required fields: {', '.join(missing)}")

    return RunSpec(
        instance_name=str(merged["instance_name"]),
        seed=int(merged["seed"]),
        depots=int(merged["depots"]),
        customers=int(merged["customers"]),
        salesmen=int(merged["salesmen"]),
        width=float(merged["width"]),
        height=float(merged["height"]),
        return_to_depot=bool(merged["return_to_depot"]),
        improve_iterations=int(merged["improve_iterations"]),
        output_json=bool(merged["output_json"]),
        output_path=str(merged.get("output_path", "")),
    )


def parse_config(config_path: Path) -> list[RunSpec]:
    data = load_json(config_path)

    if "runs" in data:
        global_config = dict(data.get("global", {}))
        return [merge_run(global_config, run) for run in data["runs"]]

    return [merge_run({}, data)]


def build_command(executable: Path, spec: RunSpec) -> list[str]:
    command = [
        str(executable),
        "--instance-name",
        spec.instance_name,
        "--seed",
        str(spec.seed),
        "--depots",
        str(spec.depots),
        "--customers",
        str(spec.customers),
        "--salesmen",
        str(spec.salesmen),
        "--width",
        str(spec.width),
        "--height",
        str(spec.height),
        "--improve-iters",
        str(spec.improve_iterations),
    ]

    command.append("--closed" if spec.return_to_depot else "--open")

    if spec.output_json:
        command.append("--json")

    if spec.output_path:
        command.extend(["--output", spec.output_path])

    return command


def ensure_parent(path: Path) -> None:
    if path.parent:
        path.parent.mkdir(parents=True, exist_ok=True)


def save_stdout(log_path: Path, content: str) -> None:
    ensure_parent(log_path)
    log_path.write_text(content, encoding="utf-8")


def run_one(
    executable: Path,
    spec: RunSpec,
    logs_dir: Path,
    dry_run: bool,
) -> dict[str, Any]:
    command = build_command(executable, spec)
    quoted = " ".join(shlex.quote(part) for part in command)

    if dry_run:
        return {
            "instance_name": spec.instance_name,
            "seed": spec.seed,
            "status": "dry_run",
            "command": quoted,
            "return_code": 0,
            "log_path": "",
            "result_path": spec.output_path,
        }

    completed = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
    )

    log_path = logs_dir / f"{spec.instance_name}_seed_{spec.seed}.log"
    save_stdout(log_path, completed.stdout + completed.stderr)

    return {
        "instance_name": spec.instance_name,
        "seed": spec.seed,
        "status": "ok" if completed.returncode == 0 else "failed",
        "command": quoted,
        "return_code": completed.returncode,
        "log_path": str(log_path),
        "result_path": spec.output_path,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--logs-dir", type=Path, default=Path("results/logs"))
    parser.add_argument("--summary-path", type=Path, default=Path("results/run_summary.json"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    specs = parse_config(args.config)
    args.logs_dir.mkdir(parents=True, exist_ok=True)
    ensure_parent(args.summary_path)

    summary: list[dict[str, Any]] = []
    failures = 0

    for spec in specs:
        result = run_one(
            executable=args.executable,
            spec=spec,
            logs_dir=args.logs_dir,
            dry_run=args.dry_run,
        )
        summary.append(result)
        print(f"[{result['status']}] {spec.instance_name} seed={spec.seed}")
        if result["status"] == "failed":
            failures += 1

    args.summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    return 1 if failures > 0 else 0


if __name__ == "__main__":
    sys.exit(main())