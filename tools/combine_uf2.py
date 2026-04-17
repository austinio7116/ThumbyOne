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

    # BOOTSEL and picotool treat each family ID as a separate
    # "sub-file" inside the UF2 container. block_no/num_blocks are
    # numbered PER FAMILY, not globally. Preserve that — renumber
    # within each family so the per-family sequences stay
    # consistent across the combined file.
    #
    # Note on the DATA family (0xe48bff57): picotool emits it with
    # num_blocks=2 but only ships 1 block per per-slot UF2 — the
    # "other" block is a convention, not a literal missing block.
    # Working firmware UF2s (e.g. ThumbyNES's) are the same. We
    # preserve the claimed num_blocks verbatim for DATA and only
    # renumber code families strictly.
    families_present = {b["family"] for b in all_blocks if (b["flags"] & 0x2000)}
    if len(families_present) > 1:
        print(f"  combined UF2 contains families: "
              f"{sorted(hex(f) for f in families_present)}",
              file=sys.stderr)

    # Count per-family
    per_fam_total = {}
    for b in all_blocks:
        key = b["family"] if (b["flags"] & 0x2000) else None
        per_fam_total[key] = per_fam_total.get(key, 0) + 1

    # Renumber per family. For multi-block families (code), rewrite
    # block_no to the within-family index and num_blocks to the
    # within-family total. For single-block families like DATA,
    # leave the numbers picotool emitted alone (BOOTSEL is used to
    # that shape).
    per_fam_index = {}
    with out_path.open("wb") as f:
        for b in all_blocks:
            key = b["family"] if (b["flags"] & 0x2000) else None
            idx = per_fam_index.get(key, 0)
            per_fam_index[key] = idx + 1
            fam_total = per_fam_total[key]

            raw = bytearray(b["raw"])
            if fam_total > 1:
                # Genuine multi-block family — renumber strictly.
                struct.pack_into("<I", raw, 5 * 4, idx)
                struct.pack_into("<I", raw, 6 * 4, fam_total)
            # else: single-block family (DATA metadata) — leave
            # block_no/num_blocks as picotool originally set them.
            f.write(bytes(raw))

        # Report what we produced
        print(f"  per-family block counts: "
              + ", ".join(
                  f"{'no-fam' if k is None else hex(k)}={v}"
                  for k, v in per_fam_total.items()
              ), file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", required=True, type=Path,
                    help="Output combined UF2 path")
    ap.add_argument("--rebase", action="append", default=[],
                    metavar="FILE=OLD:NEW",
                    help="Rebase an input's target addresses. "
                         "Example: --rebase app.uf2=0x10000000:0x10020000 "
                         "adds (NEW-OLD) to every block's target_addr "
                         "in that file. May be specified multiple times.")
    ap.add_argument("inputs", nargs="+", type=Path,
                    help="Input UF2 files (in flash-region order)")
    args = ap.parse_args()

    # Parse rebase specs: {file_stem_or_name: (old, new)}
    rebase_map = {}
    for spec in args.rebase:
        try:
            file_part, range_part = spec.split("=", 1)
            old_str, new_str = range_part.split(":", 1)
            rebase_map[file_part] = (int(old_str, 0), int(new_str, 0))
        except Exception as e:
            raise SystemExit(f"Bad --rebase spec {spec!r}: {e}")

    all_blocks = []
    for p in args.inputs:
        blocks = read_uf2_blocks(p)
        delta = 0
        for key, (old, new) in rebase_map.items():
            if p.name == key or p.stem == key or str(p) == key:
                delta = new - old
                break
        if delta != 0:
            print(f"  rebasing {p.name} by +0x{delta:08x}")
            kept = []
            FLASH_END = 0x11000000   # 16 MB flash on Thumby Color
            dropped_oob = 0
            for b in blocks:
                new_addr = b["target_addr"] + delta
                if new_addr + b["payload_size"] > FLASH_END:
                    # Block rebased past end-of-flash — usually a
                    # picotool metadata stamp at 0x10FFFF00 that
                    # every UF2 carries. The lobby's (un-rebased)
                    # copy is kept; drop this one so we don't write
                    # garbage to a wrapped address.
                    dropped_oob += 1
                    continue
                b["target_addr"] = new_addr
                raw = bytearray(b["raw"])
                struct.pack_into("<I", raw, 3 * 4, new_addr)
                b["raw"] = bytes(raw)
                kept.append(b)
            if dropped_oob:
                print(f"    dropped {dropped_oob} out-of-flash "
                      f"block(s) after rebase")
            blocks = kept
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
