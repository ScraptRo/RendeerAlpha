#!/usr/bin/env bash
# Build, then run the Sandbox from the directory its assets were copied into.
# Assets resolve relative to the working directory, so running it from anywhere else
# fails to find the font and the engine stops with a message about it.
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="${1:-Debug}"
./scripts/build.sh "$CONFIG"

cd "build/unix/bin/${CONFIG}"
exec ./sandbox
