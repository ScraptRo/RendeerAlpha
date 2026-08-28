#!/usr/bin/env bash
# Configure and build.
#
#   ./scripts/build.sh            Debug
#   ./scripts/build.sh Release
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="${1:-Debug}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake is not installed. On Debian/Ubuntu: sudo apt install cmake ninja-build" >&2
	exit 1
fi

cmake --preset unix
cmake --build build/unix --config "$CONFIG"

echo
echo "Built into build/unix/bin/${CONFIG}"
