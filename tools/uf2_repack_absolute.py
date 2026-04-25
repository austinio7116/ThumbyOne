#!/usr/bin/env python3
"""
Rewrite every UF2 block's family ID to "absolute" (0xe48bff57).

Use case: produce a "recovery" UF2 that an older ThumbyOne deployment
(whose embedded partition table only permits the absolute family) will
accept via drag-and-drop. The block payloads and target addresses are
left untouched — only the 4-byte family field at offset 28 of each
512-byte block is rewritten, and only when the block's flags have the
FAMILY_ID_PRESENT bit set.

Usage:
    python3 uf2_repack_absolute.py input.uf2 output.uf2
"""
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
FLAG_FAMILY_ID_PRESENT = 0x00002000
ABSOLUTE_FAMILY = 0xe48bff57


def repack(in_path: str, out_path: str) -> None:
    with open(in_path, "rb") as f:
        data = bytearray(f.read())
    if len(data) % 512 != 0:
        raise SystemExit(f"{in_path}: not a multiple of 512 bytes ({len(data)})")
    n = len(data) // 512

    by_family = {}
    rewritten = 0
    for i in range(n):
        off = i * 512
        m0, m1, flags = struct.unpack_from("<III", data, off)
        end = struct.unpack_from("<I", data, off + 508)[0]
        if (m0, m1, end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise SystemExit(f"block {i}: bad UF2 magic")
        if not (flags & FLAG_FAMILY_ID_PRESENT):
            by_family[None] = by_family.get(None, 0) + 1
            continue
        fam = struct.unpack_from("<I", data, off + 28)[0]
        by_family[fam] = by_family.get(fam, 0) + 1
        if fam != ABSOLUTE_FAMILY:
            struct.pack_into("<I", data, off + 28, ABSOLUTE_FAMILY)
            rewritten += 1

    with open(out_path, "wb") as f:
        f.write(data)

    print(f"input:  {in_path}  ({n} blocks)")
    for fam, count in sorted(by_family.items(), key=lambda kv: kv[1], reverse=True):
        tag = "<no family flag>" if fam is None else f"0x{fam:08x}"
        print(f"  {count:5} blocks original family={tag}")
    print(f"output: {out_path}")
    print(f"  rewrote {rewritten}/{n} blocks → family=absolute (0x{ABSOLUTE_FAMILY:08x})")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    repack(sys.argv[1], sys.argv[2])
