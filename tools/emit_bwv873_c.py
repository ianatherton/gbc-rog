#!/usr/bin/env python3
"""Legacy: emitted src/bwv873_music.* (removed). Game BGM is tools/emit_bwv1043_c.py + scraps/bwv1043_gbc.h."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "bwv873_music.c"
INP = ROOT / "tools" / "tomita_bwv873.in.c"

# Fraction of total melody MIDI ticks treated as prelude (menu loop). Tune by ear vs. score.
PRELUDE_FRAC = 0.38


def main() -> None:
    text = INP.read_text(encoding="utf-8")
    defs = {}
    for m in re.finditer(r"^#define\s+(\w+)\s+(\d+)\s*$", text, re.MULTILINE):
        defs[m.group(1)] = int(m.group(2))
    defs["REST"] = 0
    defs["END_NOTE"] = 0xFFFF
    defs["END_DUR"] = 0xFF

    def parse_array(name: str) -> list[tuple[int, int]]:
        i = text.find(f"const GBNote {name}[]")
        if i < 0:
            i = text.find(f"GBNote {name}[]")
        if i < 0:
            raise SystemExit(f"array {name} not found")
        j = text.find("{", i)
        k = text.find("};", j)
        block = text[j : k + 1]
        out = []
        pat = re.compile(r"\{\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+|\d+)\s*\}")
        for m in pat.finditer(block):
            note, ds = m.group(1), m.group(2)
            if ds.isdigit():
                dur = int(ds)
            elif ds in defs:
                dur = defs[ds]
            else:
                raise SystemExit(f"bad dur token {ds}")
            if note not in defs:
                raise SystemExit(f"unknown symbol {note}")
            out.append((defs[note], dur))
        return out

    mel = parse_array("MELODY")
    bas = parse_array("BASS")
    if len(mel) < 2 or mel[-1][0] != 0xFFFF:
        raise SystemExit("expected trailing END_NOTE sentinel on MELODY")
    if len(bas) < 2 or bas[-1][0] != 0xFFFF:
        raise SystemExit("expected trailing END_NOTE sentinel on BASS")

    # Prelude / fugue split: same wall-clock on both tracks (96 BPM, 120 TPB).
    total_ticks = sum(d for _, d in mel[:-1])
    # Prelude ~38% of piece by tick count (tune by ear vs score).
    target = int(total_ticks * PRELUDE_FRAC)
    cum = 0
    mel_pre_end = 0
    for idx, (_, d) in enumerate(mel[:-1]):  # exclude END row
        cum += d
        if cum >= target:
            mel_pre_end = idx + 1
            break
    cum = 0
    bas_pre_end = 0
    for idx, (_, d) in enumerate(bas[:-1]):
        cum += d
        if cum >= target:
            bas_pre_end = idx + 1
            break

    lines = []
    lines.append("/* Auto-generated from tools/tomita_bwv873.in.c — do not edit by hand */")
    lines.append('#include <stdint.h>')
    lines.append('#include "bwv873_music.h"')
    lines.append("")

    def emit(name: str, rows: list[tuple[int, int]]) -> None:
        lines.append(f"const GBNote {name}[] = {{")
        for f, d in rows:
            lines.append(f"    {{ {f}u, {d}u }},")
        lines.append("};")
        lines.append("")

    emit("bwv873_melody", mel)
    emit("bwv873_bass", bas)

    SRC.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {SRC} ({len(mel)} mel, {len(bas)} bas)")
    print(f"Prelude split ({PRELUDE_FRAC:.0%} of mel ticks): mel={mel_pre_end} bas={bas_pre_end} total_ticks={total_ticks}")
    print(f"Paste into src/bwv873_music.h:")
    print(f"#define BWV873_PRELUDE_END_MELODY {mel_pre_end}u")
    print(f"#define BWV873_PRELUDE_END_BASS  {bas_pre_end}u")


if __name__ == "__main__":
    main()
