from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


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
    parser.add_argument("name", help="Algorithm id, e.g. greedy_relocation")
    parser.add_argument("--display-name", default=None)
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


def update_suites(name: str) -> int:
    suites_dir = ROOT / "configs" / "suites"
    if not suites_dir.exists():
        return 0

    changed = 0

    for path in sorted(suites_dir.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))

        if "algorithms" in data:
            algorithms = data["algorithms"]
            if not isinstance(algorithms, list):
                raise ValueError(f"{path}: field 'algorithms' must be a list")
        elif "algorithm" in data:
            algorithms = [data.pop("algorithm")]
        else:
            continue

        if name not in algorithms:
            algorithms.append(name)
            data["algorithms"] = algorithms
            path.write_text(
                json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            changed += 1

    return changed


def create_cpp(name: str, force: bool) -> None:
    path = ROOT / "src" / "mdmtsp" / f"{name}.cpp"
    if path.exists() and not force:
        raise FileExistsError(f"file already exists: {path}")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(CPP_TEMPLATE.format(name=name), encoding="utf-8")


def update_cmake(name: str) -> bool:
    path = ROOT / "CMakeLists.txt"
    text = read_text(path)
    entry = f"    src/mdmtsp/{name}.cpp\n"

    if entry in text:
        return False

    marker = "    src/mdmtsp/objective.cpp\n"
    if marker not in text:
        raise ValueError("cannot find insertion point in CMakeLists.txt")

    text = text.replace(marker, marker + entry)
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

    if declaration in text:
        return False

    needle = "solve_mdmtsp_random_insertion"
    pos = text.find(needle)

    if pos == -1:
        text = text.rstrip() + "\n\n" + declaration + "\n"
    else:
        end = text.find(");\n", pos)
        if end == -1:
            raise ValueError("cannot find end of random_insertion declaration")
        insert_pos = end + len(");\n")
        text = text[:insert_pos] + "\n" + declaration + text[insert_pos:]

    write_text(path, text)
    return True


def update_main(name: str) -> bool:
    path = ROOT / "src" / "app" / "main.cpp"
    text = read_text(path)

    changed = False

    alias_block = (
        f'    if (value == "{name}") {{\n'
        f'        return "{name}";\n'
        f"    }}\n\n"
    )

    if alias_block not in text:
        marker = '    throw std::invalid_argument("unsupported algorithm: " + std::string(value));'
        if marker not in text:
            raise ValueError("cannot find canonical_algorithm_id insertion point")
        text = text.replace(marker, alias_block + marker)
        changed = True

    solve_block = (
        f'    if (algorithm_id == "{name}") {{\n'
        f"        return mdmtsp::solve_mdmtsp_{name}(instance, rng);\n"
        f"    }}\n\n"
    )

    if solve_block not in text:
        marker = '    throw std::invalid_argument("unsupported algorithm: " + algorithm_id);'
        if marker not in text:
            raise ValueError("cannot find solve_with_algorithm insertion point")
        text = text.replace(marker, solve_block + marker)
        changed = True

    if changed:
        write_text(path, text)

    return changed


def main() -> int:
    args = parse_args()

    try:
        name = validate_name(args.name)

        create_cpp(name, args.force)
        cmake_changed = update_cmake(name)
        header_changed = update_header(name)
        main_changed = update_main(name)
        suites_changed = update_suites(name)

        print(f"algorithm added: {name}")
        print(f"created: src/mdmtsp/{name}.cpp")
        print(f"updated CMakeLists.txt: {cmake_changed}")
        print(f"updated mdmtsp_solver.hpp: {header_changed}")
        print(f"updated main.cpp: {main_changed}")
        print(f"updated suite files: {suites_changed}")

        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())