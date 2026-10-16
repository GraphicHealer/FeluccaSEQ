#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Size report for the last ./build.sh: where the flash and RAM bytes are.

  tools/size_report.py            # report on build/felucca.elf
  tools/size_report.py --top 30   # more symbols per region

Per firmware/app.ld's MEMORY regions it lists each region's fill and its largest
symbols (nm -S), then sizeof the sequencer's own structures (tests/struct_sizes.c,
host cc). Symbols whose home is RAMTEXT are counted against XIP too — the flash
image stores them there (app.ld: AT > XIP), as .data's initial values are.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
OUT = SRC / "build"
ELF = OUT / "felucca.elf"
TC = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain"))
NM = TC / "pi32v2" / "bin" / "nm"
TOP = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 15

MEM = re.compile(r"^\s*(\w+)\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+|\d+K?)\s*,\s*LENGTH\s*=\s*(0x[0-9a-fA-F]+|\d+K?)\s*$", re.M)


def num(s):
    return int(s[:-1]) * 1024 if s.endswith("K") else int(s, 0)


def regions():
    r = {}
    for m in MEM.finditer((SRC / "firmware" / "app.ld").read_text()):
        r[m.group(1)] = (num(m.group(2)), num(m.group(3)))
    return r


def symbols():
    out = subprocess.run([str(NM), "--print-size", "--radix=d", ELF], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and p[0].isdigit() and p[1].isdigit():
            yield int(p[0]), int(p[1]), p[2], p[3]


def main():
    if not ELF.exists():
        raise SystemExit(f"{ELF}: run ./build.sh first")
    reg = regions()
    buckets = {n: [] for n in reg}
    lost = []
    for a, s, t, name in symbols():
        for n, (o, ln) in reg.items():
            if o <= a < o + ln:
                buckets[n].append((s, name, t))
                break
        else:
            lost.append((s, name))

    img = (OUT / "felucca.bin").stat().st_size
    print(f"== build/felucca.elf  (app image {img:,} B)\n")
    for n, (o, ln) in reg.items():
        used = sum(s for s, _, _ in buckets[n])
        print(f"{n:8} {used:>9,} B of {ln:>9,} B  ({100 * used / ln:4.1f}% full, {ln - used:,} B free)")
        for s, name, t in sorted(buckets[n], reverse=True)[:TOP]:
            print(f"    {s:>9,} {t} {name}")
        print()
    if lost:
        print(f"outside the regions: {sum(s for s, _ in lost):,} B "
              f"({', '.join(n for _, n in sorted(lost, reverse=True)[:6])})")
        print()
    # the XIP image also holds .data's values and .ram_text/.c1_text (AT > XIP)
    flash_data = sum(s for s, _, t in buckets["RAM"] if t in "dDrR")
    flash_rt = sum(s for s, _, _ in buckets["RAMTEXT"])
    xip_code = sum(s for s, _, _ in buckets["XIP"])
    print(f"flash image parts: text+rodata {xip_code:,} + ram_text/c1_text {flash_rt:,} "
          f"+ .data values {flash_data:,} = {xip_code + flash_rt + flash_data:,} B (image {img:,} B)")
    print()
    probe = OUT / "struct_sizes"
    subprocess.run(["cc", "-O1", "-w", "-Ibuild/gen", "-Ifirmware/src",
                    "-o", probe, SRC / "tests" / "struct_sizes.c", "-lm"], check=True, cwd=SRC)
    print(subprocess.run([str(probe)], check=True, capture_output=True, text=True).stdout)


if __name__ == "__main__":
    main()
