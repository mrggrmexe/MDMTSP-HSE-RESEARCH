#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_NAME="$(basename "$0")"

usage() {
  cat <<USAGE
Usage: $SCRIPT_NAME [options]

Quick rebuild script for macOS CMake projects.
Run it from the repository root or pass --source-dir explicitly.

Options:
  --source-dir PATH         Source directory with CMakeLists.txt (default: .)
  --build-dir PATH          Build directory (default: build)
  --generator NAME          CMake generator, e.g. Ninja or Xcode
  --build-type TYPE         Debug | Release | RelWithDebInfo | MinSizeRel
                            Default: Release
  --jobs N                  Parallel build jobs (default: auto-detect)
  --target NAME             Build only a specific target
  --fresh                   Delete build directory before configuring
  --clean-first             Build with --clean-first
  --reconfigure-only        Configure only, do not build
  --skip-configure          Skip configure step and only build existing tree
  --tests                   Run ctest after successful build
  --no-tests                Do not run tests (default)
  --install                 Run cmake --install after build
  --prefix PATH             Install prefix for cmake --install
  --verbose                 Verbose build
  --arm64                   Set CMAKE_OSX_ARCHITECTURES=arm64
  --x86_64                  Set CMAKE_OSX_ARCHITECTURES=x86_64
  --universal               Set CMAKE_OSX_ARCHITECTURES=arm64;x86_64
  -DVAR=VALUE               Forward arbitrary cache definitions to CMake
  -h, --help                Show this help

Examples:
  ./$SCRIPT_NAME
  ./$SCRIPT_NAME --fresh --tests
  ./$SCRIPT_NAME --build-type Debug --generator Ninja
  ./$SCRIPT_NAME --target mdmtsp --clean-first
USAGE
}

log() {
  printf '[%s] %s\n' "$SCRIPT_NAME" "$*"
}

fail() {
  printf '[%s] ERROR: %s\n' "$SCRIPT_NAME" "$*" >&2
  exit 1
}

on_error() {
  local exit_code=$?
  local line_no=${1:-unknown}
  printf '[%s] ERROR: command failed at line %s with exit code %s\n' \
    "$SCRIPT_NAME" "$line_no" "$exit_code" >&2
  exit "$exit_code"
}
trap 'on_error $LINENO' ERR

resolve_jobs() {
  if command -v sysctl >/dev/null 2>&1; then
    local cpu_count
    cpu_count="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
    if [[ "$cpu_count" =~ ^[0-9]+$ ]] && (( cpu_count > 0 )); then
      printf '%s\n' "$cpu_count"
      return
    fi
  fi

  if command -v getconf >/dev/null 2>&1; then
    local cpu_count
    cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    if [[ "$cpu_count" =~ ^[0-9]+$ ]] && (( cpu_count > 0 )); then
      printf '%s\n' "$cpu_count"
      return
    fi
  fi

  printf '4\n'
}

canonicalize_dir() {
  local path="$1"
  if [[ -d "$path" ]]; then
    (cd "$path" && pwd)
  else
    local parent
    parent="$(dirname "$path")"
    local base
    base="$(basename "$path")"
    mkdir -p "$parent"
    printf '%s/%s\n' "$(cd "$parent" && pwd)" "$base"
  fi
}

SOURCE_DIR="."
BUILD_DIR="build"
GENERATOR=""
BUILD_TYPE="Release"
JOBS=""
TARGET=""
FRESH=0
CLEAN_FIRST=0
RECONFIGURE_ONLY=0
SKIP_CONFIGURE=0
RUN_TESTS=0
RUN_INSTALL=0
VERBOSE=0
INSTALL_PREFIX=""
ARCH_VALUE=""

CMAKE_DEFINES=()
BUILD_ARGS=()
INSTALL_ARGS=()

while (($# > 0)); do
  case "$1" in
    --source-dir)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      SOURCE_DIR="$2"
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      BUILD_DIR="$2"
      shift 2
      ;;
    --generator)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      GENERATOR="$2"
      shift 2
      ;;
    --build-type)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      BUILD_TYPE="$2"
      shift 2
      ;;
    --jobs)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      JOBS="$2"
      shift 2
      ;;
    --target)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      TARGET="$2"
      shift 2
      ;;
    --fresh)
      FRESH=1
      shift
      ;;
    --clean-first)
      CLEAN_FIRST=1
      shift
      ;;
    --reconfigure-only)
      RECONFIGURE_ONLY=1
      shift
      ;;
    --skip-configure)
      SKIP_CONFIGURE=1
      shift
      ;;
    --tests)
      RUN_TESTS=1
      shift
      ;;
    --no-tests)
      RUN_TESTS=0
      shift
      ;;
    --install)
      RUN_INSTALL=1
      shift
      ;;
    --prefix)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      INSTALL_PREFIX="$2"
      shift 2
      ;;
    --verbose)
      VERBOSE=1
      shift
      ;;
    --arm64)
      ARCH_VALUE="arm64"
      shift
      ;;
    --x86_64)
      ARCH_VALUE="x86_64"
      shift
      ;;
    --universal)
      ARCH_VALUE="arm64;x86_64"
      shift
      ;;
    -D*)
      CMAKE_DEFINES+=("$1")
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

case "$BUILD_TYPE" in
  Debug|Release|RelWithDebInfo|MinSizeRel)
    ;;
  *)
    fail "unsupported build type: $BUILD_TYPE"
    ;;
esac

if [[ -n "$JOBS" ]] && [[ ! "$JOBS" =~ ^[0-9]+$ ]]; then
  fail "--jobs must be a positive integer"
fi
if [[ -n "$JOBS" ]] && (( JOBS <= 0 )); then
  fail "--jobs must be greater than zero"
fi

if (( RECONFIGURE_ONLY == 1 && SKIP_CONFIGURE == 1 )); then
  fail "--reconfigure-only and --skip-configure cannot be used together"
fi

if (( RUN_INSTALL == 1 )) && (( RECONFIGURE_ONLY == 1 )); then
  fail "--install cannot be combined with --reconfigure-only"
fi

SOURCE_DIR="$(canonicalize_dir "$SOURCE_DIR")"
BUILD_DIR="$(canonicalize_dir "$BUILD_DIR")"

[[ -f "$SOURCE_DIR/CMakeLists.txt" ]] || fail "CMakeLists.txt not found in source dir: $SOURCE_DIR"

if [[ "$SOURCE_DIR" == "$BUILD_DIR" ]]; then
  fail "source and build directories must be different"
fi

if [[ -z "$JOBS" ]]; then
  JOBS="$(resolve_jobs)"
fi

if (( FRESH == 1 )); then
  log "Removing build directory: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

CONFIGURE_CMD=(cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" "-DCMAKE_BUILD_TYPE=$BUILD_TYPE")

if [[ -n "$GENERATOR" ]]; then
  CONFIGURE_CMD+=(-G "$GENERATOR")
fi

if [[ -n "$ARCH_VALUE" ]]; then
  CONFIGURE_CMD+=("-DCMAKE_OSX_ARCHITECTURES=$ARCH_VALUE")
fi

if (( RUN_TESTS == 1 )); then
  CONFIGURE_CMD+=("-DMDMTSP_BUILD_TESTS=ON")
fi

if [[ -n "$INSTALL_PREFIX" ]]; then
  CONFIGURE_CMD+=("-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX")
fi

if ((${#CMAKE_DEFINES[@]} > 0)); then
  CONFIGURE_CMD+=("${CMAKE_DEFINES[@]}")
fi

BUILD_CMD=(cmake --build "$BUILD_DIR" --parallel "$JOBS")

if [[ -n "$TARGET" ]]; then
  BUILD_CMD+=(--target "$TARGET")
fi

if (( CLEAN_FIRST == 1 )); then
  BUILD_CMD+=(--clean-first)
fi

if (( VERBOSE == 1 )); then
  BUILD_CMD+=(--verbose)
fi

TEST_CMD=(ctest --test-dir "$BUILD_DIR" --output-on-failure)
if [[ -n "$JOBS" ]]; then
  TEST_CMD+=(--parallel "$JOBS")
fi

INSTALL_CMD=(cmake --install "$BUILD_DIR")
if [[ -n "$INSTALL_PREFIX" ]]; then
  INSTALL_CMD+=(--prefix "$INSTALL_PREFIX")
fi

log "Source dir : $SOURCE_DIR"
log "Build dir  : $BUILD_DIR"
log "Build type : $BUILD_TYPE"
log "Jobs       : $JOBS"
if [[ -n "$GENERATOR" ]]; then
  log "Generator  : $GENERATOR"
fi
if [[ -n "$ARCH_VALUE" ]]; then
  log "Architect. : $ARCH_VALUE"
fi

if (( SKIP_CONFIGURE == 0 )); then
  log "Configuring project"
  "${CONFIGURE_CMD[@]}"
else
  [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || fail "build cache not found in $BUILD_DIR; cannot use --skip-configure"
  log "Skipping configure step"
fi

if (( RECONFIGURE_ONLY == 1 )); then
  log "Configure completed"
  exit 0
fi

log "Building project"
"${BUILD_CMD[@]}"

if (( RUN_TESTS == 1 )); then
  if [[ -f "$BUILD_DIR/CTestTestfile.cmake" || -d "$BUILD_DIR/Testing" ]]; then
    log "Running tests"
    "${TEST_CMD[@]}"
  else
    log "Tests requested, but no CTest metadata was found; skipping"
  fi
fi

if (( RUN_INSTALL == 1 )); then
  log "Installing project"
  "${INSTALL_CMD[@]}"
fi

if [[ -x "$BUILD_DIR/bin/mdmtsp" ]]; then
  log "Binary ready: $BUILD_DIR/bin/mdmtsp"
fi

log "Done"
