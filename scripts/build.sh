#!/usr/bin/env bash
# Build NxfireDB (Linux/macOS).
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build"
echo "Build selesai. Hasil di $ROOT/build/"
