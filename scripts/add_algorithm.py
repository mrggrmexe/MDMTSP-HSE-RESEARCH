from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent.parent

CPP_TEMPLATE = '''#include "mdmtsp_solver.hpp"

#include <stdexcept>

namespace mdmtsp {{

MDMTSPSolution solve_mdmtsp_{name}(
    const MDMTSPInstance& instance,
    Random& rng
) {{
    (void)rng;

    instance.validate_basic();

    throw std::logic_error("algorithm '{name}' is not implemented yet");
}}

}}  // namespace mdmtsp
'''


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("name", help="Algorithm id, for example: cheapest_insertion")
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def validate_name(name: str) -> str:
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        raise ValueError("algorithm name must match [a-z][a-z0-9_]*")
    return name


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: Any) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def create_cpp_file(name: str, force: bool) -> bool:
    path = ROOT / "src" / "mdmtsp" / f"{name}.cpp"

    if path.exists() and not force:
        return False

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(CPP_TEMPLATE.format(name=name), encoding="utf-8")
    return True


def update_cmake(name: str) -> bool:
    path = ROOT / "CMakeLists.txt"
    text = read_text(path)

    entry = f"    src/mdmtsp/{name}.cpp\n"
    if entry in text:
        return False

    pattern = re.compile(
        r"(add_library\(mdmtsp_core\s+STATIC\s*\n)(?P<sources>.*?)(\n\))",
        re.DOTALL,
    )
    match = pattern.search(text)

    if not match:
        raise ValueError("cannot find mdmtsp_core add_library block in CMakeLists.txt")

    sources = match.group("sources")
    source_lines = [line for line in sources.splitlines() if line.strip()]

    source_lines.append(entry.rstrip("\n"))
    source_lines = sorted(dict.fromkeys(source_lines), key=lambda value: value.strip())

    replacement = (
        match.group(1)
        + "\n".join(source_lines)
        + match.group(3)
    )

    text = text[:match.start()] + replacement + text[match.end():]
    write_text(path, text)
    return True


def update_header(name: str) -> bool:
    path = ROOT / "src" / "mdmtsp" / "mdmtsp_solver.hpp"
    text = read_text(path)

    declaration = (
        f"[[nodiscard]] MDMTSPSolution solve_mdmtsp_{name}(\n"
        f"    const MDMTSPInstance& instance,\n"
        f"    Random& rng\n"
        f");\n"
    )

    if f"solve_mdmtsp_{name}(" in text:
        return False

    namespace_end = "}  // namespace mdmtsp"
    if namespace_end in text:
        text = text.replace(namespace_end, declaration + "\n" + namespace_end)
    else:
        text = text.rstrip() + "\n\n" + declaration + "\n"

    write_text(path, text)
    return True


def update_main(name: str) -> bool:
    path = ROOT / "src" / "app" / "main.cpp"
    text = read_text(path)

    changed = False

    if f'return "{name}";' not in text:
        canonical_marker = (
            '    throw std::invalid_argument("unsupported algorithm: " + std::string(value));'
        )
        canonical_block = (
            f'    if (value == "{name}") {{\n'
            f'        return "{name}";\n'
            f"    }}\n\n"
        )

        if canonical_marker not in text:
            raise ValueError("cannot find canonical_algorithm_id insertion point in main.cpp")

        text = text.replace(canonical_marker, canonical_block + canonical_marker)
        changed = True

    if f"solve_mdmtsp_{name}(" not in text:
        solve_marker = (
            '    throw std::invalid_argument("unsupported algorithm: " + algorithm_id);'
        )
        solve_block = (
            f'    if (algorithm_id == "{name}") {{\n'
            f"        return mdmtsp::solve_mdmtsp_{name}(instance, rng);\n"
            f"    }}\n\n"
        )

        if solve_marker not in text:
            raise ValueError("cannot find solve_with_algorithm insertion point in main.cpp")

        text = text.replace(solve_marker, solve_block + solve_marker)
        changed = True

    if changed:
        write_text(path, text)

    return changed


def normalize_suite_algorithms(data: dict[str, Any], path: Path) -> list[str] | None:
    if "algorithms" in data:
        algorithms = data["algorithms"]
        if not isinstance(algorithms, list):
            raise ValueError(f"{path}: field 'algorithms' must be a list")
        if not all(isinstance(item, str) and item.strip() for item in algorithms):
            raise ValueError(f"{path}: every algorithm must be a non-empty string")
        return [item.strip() for item in algorithms]

    if "algorithm" in data:
        algorithm = data.pop("algorithm")
        if not isinstance(algorithm, str) or not algorithm.strip():
            raise ValueError(f"{path}: field 'algorithm' must be a non-empty string")
        return [algorithm.strip()]

    return None


def update_suites(name: str) -> int:
    suites_dir = ROOT / "configs" / "suites"
    if not suites_dir.exists():
        return 0

    changed_count = 0

    for path in sorted(suites_dir.glob("*.json")):
        data = load_json(path)

        if not isinstance(data, dict):
            raise ValueError(f"{path}: suite config must be a JSON object")

        algorithms = normalize_suite_algorithms(data, path)
        if algorithms is None:
            continue

        changed = "algorithm" not in data and "algorithms" in data and data["algorithms"] != algorithms

        if name not in algorithms:
            algorithms.append(name)
            changed = True

        if changed or "algorithms" not in data:
            data["algorithms"] = algorithms
            write_json(path, data)
            changed_count += 1

    return changed_count


def main() -> int:
    args = parse_args()

    try:
        name = validate_name(args.name)

        cpp_created_or_rewritten = create_cpp_file(name, args.force)
        cmake_changed = update_cmake(name)
        header_changed = update_header(name)
        main_changed = update_main(name)
        suites_changed = update_suites(name)

        print(f"algorithm: {name}")
        print(f"cpp file created/re-written: {cpp_created_or_rewritten}")
        print(f"CMakeLists.txt changed: {cmake_changed}")
        print(f"mdmtsp_solver.hpp changed: {header_changed}")
        print(f"main.cpp changed: {main_changed}")
        print(f"suite files changed: {suites_changed}")

        if not cpp_created_or_rewritten:
            print(
                f"note: src/mdmtsp/{name}.cpp already exists; use --force to rewrite it"
            )

        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())