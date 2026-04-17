#!/usr/bin/env python3
"""
Combine multiple UF2 files into one.

The UF2 format packages an image as a sequence of 512-byte blocks,
each carrying up to 256 bytes of payload tagged with a flash
address. Concatenating the blocks of multiple UF2s produces a valid
UF2 as long as:

  * every block's `block_no` field is updated to its index in the
    combined sequence;
  * every block's `num_blocks` field is set to the total combined
    count;
  * the family_id (uf2 "file_size_or_family" field when the
    family flag is set) matches across all inputs.

Block layout (UF2 1.0), little-endian:

  magic_start0  u32  0x0A324655  'UF2\\n'
  magic_start1  u32  0x9E5D5157
  flags         u32
  target_addr   u32
  payload_size  u32
  block_no      u32
  num_blocks    u32
  file_size_or_family u32
  data          u8[476]
  magic_end     u32  0x0AB16F30

This tool does a straight cat-with-renumbering. No consolidation of
adjacent blocks; no rewriting payload contents. Intended for
assembling a single flash-once UF2 from per-slot builds.
"""

import argparse
import struct
import sys
from pathlib import Path

UF2_BLOCK_SIZE     = 512
UF2_MAGIC_START0   = 0x0A324655
UF2_MAGIC_START1   = 0x9E5D5157
UF2_MAGIC_END      = 0x0AB16F30
UF2_HEADER_FMT     = "<IIIIIIIII"  # 9 u32s; payload + end magic follow
UF2_HEADER_SIZE    = struct.calcsize(UF2_HEADER_FMT)


def read_uf2_blocks(path: Path):
    data = path.read_bytes()
    if len(data) % UF2_BLOCK_SIZE != 0:
        raise SystemExit(
            f"{path}: size {len(data)} is not a multiple of 512 — "
            f"not a valid UF2"
        )
    blocks = []
    for off in range(0, len(data), UF2_BLOCK_SIZE):
        block = data[off:off + UF2_BLOCK_SIZE]
        hdr = struct.unpack(UF2_HEADER_FMT, block[:UF2_HEADER_SIZE])
        (m0, m1, flags, target_addr, payload_size,
         block_no, num_blocks, file_size_or_family, _reserved) = hdr
        (m_end,) = struct.unpack("<I", block[-4:])
        if m0 != UF2_MAGIC_START0 or m1 != UF2_MAGIC_START1 \
                or m_end != UF2_MAGIC_END:
            raise SystemExit(
                f"{path}: block at offset {off} has bad magic"
            )
        blocks.append({
            "flags":        flags,
            "target_addr":  target_addr,
            "payload_size": payload_size,
            "block_no":     block_no,
            "num_blocks":   num_blocks,
            "family":       file_size_or_family,
            "raw":          block,
        })
    return blocks


def dedupe_by_address(all_blocks):
    """Keep only the first block seen for each target address.
    picotool stamps per-image metadata at end-of-flash with family
    RP2350_DATA; when combining multiple per-slot UF2s those stamps
    collide. First-wins dedupe is fine — all copies describe the
    same flash region."""
    seen = {}
    out = []
    dropped = 0
    for b in all_blocks:
        key = b["target_addr"]
        if key in seen:
            dropped += 1
            continue
        seen[key] = True
        out.append(b)
    if dropped:
        print(f"  deduped {dropped} block(s) targeting already-covered "
              f"addresses", file=sys.stderr)
    return out


def write_combined(out_path: Path, all_blocks):
    total = len(all_blocks)
    if total == 0:
        raise SystemExit("No blocks to combine.")

    # Sanity: all blocks should share the family id (or none of them
    # use the family-id flag). We don't enforce — RP2350 UF2s mix
    # ARM_S (code) and DATA (picotool metadata) families by design.
    families = {b["family"] for b in all_blocks if (b["flags"] & 0x2000)}
    if len(families) > 1:
        print(f"  combined UF2 contains families: "
              f"{[hex(f) for f in families]}", file=sys.stderr)

    with out_path.open("wb") as f:
        for i, b in enumerate(all_blocks):
            # Rewrite block_no and num_blocks in-place.
            raw = bytearray(b["raw"])
            struct.pack_into("<I", raw, 5 * 4, i)           # block_no
            struct.pack_into("<I", raw, 6 * 4, total)       # num_blocks
            f.write(bytes(raw))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", required=True, type=Path,
                    help="Output combined UF2 path")
    ap.add_argument("inputs", nargs="+", type=Path,
                    help="Input UF2 files (in flash-region order)")
    args = ap.parse_args()

    all_blocks = []
    for p in args.inputs:
        blocks = read_uf2_blocks(p)
        all_blocks.extend(blocks)
        min_addr = min(b["target_addr"] for b in blocks)
        max_addr = max(b["target_addr"] + b["payload_size"] for b in blocks)
        print(f"  {p.name}: {len(blocks)} blocks, "
              f"0x{min_addr:08x}..0x{max_addr:08x}")

    all_blocks = dedupe_by_address(all_blocks)

    write_combined(args.output, all_blocks)
    print(f"wrote {args.output} ({len(all_blocks)} blocks, "
          f"{len(all_blocks) * UF2_BLOCK_SIZE} bytes)")


if __name__ == "__main__":
    main()
