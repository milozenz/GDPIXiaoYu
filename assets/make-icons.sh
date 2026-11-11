#!/bin/bash
# 从 assets/logo.svg 生成：客户端 app.ico（16-256 各尺寸，PNG 格式）、门户和审核后台的 favicon.svg
# 需要 rsvg-convert（apt install librsvg2-bin）和 python3
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SIZES="16 20 24 32 40 48 64 128 256"
for s in $SIZES; do rsvg-convert -w "$s" -h "$s" assets/logo.svg -o "$TMP/$s.png"; done
python3 - "$TMP" $SIZES <<'PY'
import struct, sys
tmp, sizes = sys.argv[1], [int(s) for s in sys.argv[2:]]
pngs = [open(f"{tmp}/{s}.png", "rb").read() for s in sizes]
head = struct.pack("<HHH", 0, 1, len(sizes))
off = 6 + 16 * len(sizes); entries = b""
for s, data in zip(sizes, pngs):
    entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), off); off += len(data)
open("client/XiaoYu.App/app.ico", "wb").write(head + entries + b"".join(pngs))
print("app.ico:", ", ".join(map(str, sizes)))
PY
cp assets/logo.svg portal/favicon.svg
cp assets/logo.svg client/XiaoYu.Admin/wwwroot/favicon.svg
echo "favicon.svg -> portal/、client/XiaoYu.Admin/wwwroot/"
