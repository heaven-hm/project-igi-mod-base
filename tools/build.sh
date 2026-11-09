#!/usr/bin/env sh
set -eu

platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
config=Release
demos=ON
clean=0

usage() {
    cat <<'EOF'
Usage: tools/build.sh [--build macos|linux] [--config Debug|Release] [--no-demos] [--clean]

Examples:
  tools/build.sh --build macos
  tools/build.sh --build linux --config Debug
  tools/build.sh --build macos --clean
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build) platform="$2"; shift 2 ;;
        --config) config="$2"; shift 2 ;;
        --no-demos) demos=OFF; shift ;;
        --clean) clean=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$platform" in
    macos|darwin) platform=macos ;;
    linux) platform=linux ;;
    *) echo "build.sh supports macos and linux; use build.cmd on Windows" >&2; exit 2 ;;
esac

build_dir="${TMPDIR:-/tmp}/igi-sdk-${platform}-${config}"
if [ "$clean" -eq 1 ]; then rm -rf "$build_dir"; fi

root="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$config" -DIGI_SDK_BUILD_DEMOS="$demos"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
echo "Build complete: $build_dir"
