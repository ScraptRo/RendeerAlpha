#!/usr/bin/env bash
# Build and run the tests.
#
# Three of them: the engine's own cases, which need no device, and the two binding
# tests, which start a real engine -- so a window appears for a few seconds and closes
# itself. Those two skip, with a reason, when Python, node or koffi is not installed;
# see bindings/tests/README.md.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset unix
cmake --build build/unix --config Debug
ctest --preset unix-debug
