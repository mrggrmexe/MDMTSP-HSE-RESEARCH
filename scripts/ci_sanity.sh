#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTIFACTS_DIR="${CI_ARTIFACTS_DIR:-${ROOT_DIR}/.ci-artifacts}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
CI_MAX_SOLUTION_CUSTOMERS="${CI_MAX_SOLUTION_CUSTOMERS:-500}"

mkdir -p "${ARTIFACTS_DIR}"

log() {
  printf '[ci_sanity] %s\n' "$*"
}

has_json_files() {
  local dir="$1"
  [ -d "$dir" ] && find "$dir" -type f -name '*.json' -print -quit | grep -q .
}

has_python_tests() {
  [ -d "${ROOT_DIR}/tests" ] && find "${ROOT_DIR}/tests" -type f -name 'test_*.py' -print -quit | grep -q .
}

log "Using root: ${ROOT_DIR}"
log "Artifacts dir: ${ARTIFACTS_DIR}"

cd "${ROOT_DIR}"

log "Compiling Python sources"
"${PYTHON_BIN}" -m compileall scripts tests

log "Validating JSON configs"
"${PYTHON_BIN}" - <<'PY'
from __future__ import annotations

import json
from pathlib import Path

roots = [Path("configs")]
validated = 0
for root in roots:
    if not root.exists():
        continue
    for path in sorted(root.rglob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            json.load(handle)
        validated += 1
print(f"validated {validated} JSON config file(s)")
PY

log "Syntax-checking shell scripts"
while IFS= read -r -d '' shell_file; do
  bash -n "$shell_file"
done < <(find scripts -type f \( -name '*.sh' -o -name '*.bash' -o -name '*.zsh' \) -print0)

if has_python_tests; then
  log "Running Python unit tests"
  "${PYTHON_BIN}" -m unittest discover -s tests -p 'test_*.py'
else
  log "No Python unit tests found; skipping"
fi

if [ "${CI_SKIP_INSTANCE_CHECK:-0}" != "1" ] && has_json_files "${ROOT_DIR}/instances"; then
  log "Running instance validation"
  "${PYTHON_BIN}" scripts/check_instances.py \
    "${ROOT_DIR}/instances" \
    --report-csv "${ARTIFACTS_DIR}/instance_check.csv" \
    --report-jsonl "${ARTIFACTS_DIR}/instance_check.jsonl"
else
  log "Instance validation skipped"
fi

if [ "${CI_SKIP_EXPORT_REPORT:-0}" != "1" ] && (has_json_files "${ROOT_DIR}/results/runs" || [ -d "${ROOT_DIR}/results/logs" ]); then
  log "Building Excel report from committed results"
  "${PYTHON_BIN}" scripts/export_excel_report.py \
    --runs-root "${ROOT_DIR}/results/runs" \
    --logs-root "${ROOT_DIR}/results/logs" \
    --output "${ARTIFACTS_DIR}/research_report.xlsx"
else
  log "Excel export skipped"
fi

if [ "${CI_SKIP_PLOT_RESULTS:-0}" != "1" ] && { [ -d "${ROOT_DIR}/results/tables" ] || has_json_files "${ROOT_DIR}/results/runs"; }; then
  log "Generating aggregate plots and small-solution visualizations"
  "${PYTHON_BIN}" scripts/plot_results.py \
    --tables-root "${ROOT_DIR}/results/tables" \
    --runs-root "${ROOT_DIR}/results/runs" \
    --output-root "${ARTIFACTS_DIR}/plots" \
    --solution-workers "${CI_SOLUTION_WORKERS:-2}" \
    --max-solution-customers "${CI_MAX_SOLUTION_CUSTOMERS}"
else
  log "Plot generation skipped"
fi

log "CI sanity checks completed successfully"
