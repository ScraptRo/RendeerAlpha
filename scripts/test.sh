#!/usr/bin/env bash
# Build and run the tests.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset unix
cmake --build build/unix --config Debug
ctest --preset unix-debug
