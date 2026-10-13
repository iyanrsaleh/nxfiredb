#!/usr/bin/env bash
# Paketkan hasil build menjadi folder + arsip rilis (tanpa source).
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VER=0.2.0

case "$(uname -s)" in
    Linux*)  OS=linux-$(uname -m) ;;
    Darwin*) OS=macos-$(uname -m) ;;
    *)       OS=unknown ;;
esac

OUT="$ROOT/dist/nxfire-sdk-$VER-$OS"
mkdir -p "$OUT/bin"

cp "$ROOT/build-linux/server/nxfire-server"      "$OUT/bin/"
cp "$ROOT/build-linux/client/nxfire-client"      "$OUT/bin/"
cp "$ROOT/build-linux/tools/nxfire/nxfire"       "$OUT/bin/"
cp "$ROOT/build-linux/tools/watch/nxfire-watch"  "$OUT/bin/" 2>/dev/null || true

[ -f "$ROOT/README.md" ]           && cp "$ROOT/README.md"           "$OUT/README.md"
[ -f "$ROOT/nxfire.json.example" ] && cp "$ROOT/nxfire.json.example" "$OUT/"
cp "$ROOT/nxfire-sdk.json" "$OUT/nxfire-sdk.json"

# Izin eksekusi biner Linux.
chmod +x "$OUT"/bin/* 2>/dev/null || true

( cd "$ROOT/dist" && tar -czf "nxfire-sdk-$VER-$OS.tar.gz" "nxfire-sdk-$VER-$OS" )

echo
echo "Paket rilis siap di: $OUT"
echo "Arsip: $ROOT/dist/nxfire-sdk-$VER-$OS.tar.gz"
