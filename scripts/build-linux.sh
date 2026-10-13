#!/usr/bin/env bash
# Build NxfireDB (Linux/macOS) ke folder build terpisah: build-linux.
# Folder build/ dipakai Windows, jadi tidak boleh tertimpa.
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$ROOT" -B "$ROOT/build-linux" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-linux" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "Build Linux selesai. Hasil di $ROOT/build-linux/"
