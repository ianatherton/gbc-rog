#!/usr/bin/env python3
"""Parse res MIDI for BWV 527 organ trio sonata → dictionary-packed src/bwv527_music.*

Uses tracks 1+2 merged by highest pitch (manual parts), track 3 lowest pitch (pedal).
Requires mido: use project venv (tools/../.venv/bin/pip install mido)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import mido
except ImportError:
    print("Install mido: python3 -m venv .venv && .venv/bin/pip install mido", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
MID_PATH = ROOT / "res" / "bach_organ_trio_sonata_527_(c)simonetto.mid"
SRC_C = ROOT / "src" / "bwv527_music.c"
SRC_H = ROOT / "src" / "bwv527_music.h"

PRELUDE_FRAC = 0.35
VBL_HZ = 59.7275
# Nominal tempo ~79 BPM from MIDI — tune emitter BPM for in-game perceived speed vs BWV 1043
SCORE_BPM = 79.0
SENTINEL_IDX = 0xFF


def midi_to_gb_freq_reg(midi: int) -> int:
    hz = 440.0 * (2.0 ** ((float(midi) - 69.0) / 12.0))
    return int(2048.0 - 131072.0 / hz) & 0xFFFF


def parse_track_notes(track: mido.MidiTrack) -> list[tuple[int, int, int]]:
    t = 0
    active: dict[int, int] = {}
    intervals: list[tuple[int, int, int]] = []
    for msg in track:
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0:
            active[msg.note] = t
        elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
            st = active.pop(msg.note, None)
            if st is not None:
                intervals.append((st, t, msg.note))
    return intervals


def monophonic_reduce(intervals: list[tuple[int, int, int]], take_max: bool) -> list[tuple[int, int, int]]:
    if not intervals:
        return []
    pts: set[int] = set()
    for s, e, _ in intervals:
        pts.add(s)
        pts.add(e)
    ordered = sorted(pts)
    out: list[tuple[int, int, int]] = []
    for i in range(len(ordered) - 1):
        a, b = ordered[i], ordered[i + 1]
        active = []
        for s, e, p in intervals:
            if s < b and e > a:
                active.append(p)
        if not active:
            continue
        p = max(active) if take_max else min(active)
        out.append((a, b, p))
    merged: list[tuple[int, int, int]] = []
    for seg in out:
        if merged and merged[-1][2] == seg[2] and merged[-1][1] == seg[0]:
            merged[-1] = (merged[-1][0], seg[1], seg[2])
        else:
            merged.append(seg)
    return merged


def snap_segments_to_grid(
    segments: list[tuple[int, int, int]], sixteenth_ticks: int, end_tick: int
) -> list[tuple[int, int]]:
    piece_16 = max(1, round(end_tick / sixteenth_ticks))
    notes: list[tuple[int, int]] = []
    cur_i = 0
    for s, e, p in segments:
        si = round(s / sixteenth_ticks)
        ei = round(e / sixteenth_ticks)
        si = max(si, cur_i)
        if ei <= si:
            ei = si + 1
        ei = min(ei, piece_16)
        if si > piece_16:
            break
        if si > cur_i:
            notes.append((0, si - cur_i))
            cur_i = si
        if cur_i < ei:
            notes.append((p, ei - cur_i))
            cur_i = ei
    if cur_i < piece_16:
        notes.append((0, piece_16 - cur_i))
    return notes


def prelude_indices(v1: list[tuple[int, int]], v2: list[tuple[int, int]]) -> tuple[int, int]:
    total_16 = sum(d for _, d in v1)
    target = int(total_16 * PRELUDE_FRAC)
    cum, mel_end = 0, 0
    for idx, (_, d) in enumerate(v1):
        cum += d
        if cum >= target:
            mel_end = idx + 1
            break
    cum, bas_end = 0, 0
    for idx, (_, d) in enumerate(v2):
        cum += d
        if cum >= target:
            bas_end = idx + 1
            break
    return mel_end, bas_end


def main() -> None:
    if not MID_PATH.is_file():
        raise SystemExit(f"MIDI not found: {MID_PATH}")
    m = mido.MidiFile(str(MID_PATH))
    if m.type != 1 or len(m.tracks) < 4:
        raise SystemExit("expected type-1 MIDI with tracks 1–3 as music")
    tpb = m.ticks_per_beat
    six = tpb // 4
    end_tick = 0
    for tr in m.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            end_tick = max(end_tick, t)

    intv1 = parse_track_notes(m.tracks[1])
    intv2 = parse_track_notes(m.tracks[2])
    intv3 = parse_track_notes(m.tracks[3])
    mel_seg = monophonic_reduce(intv1 + intv2, True)
    bas_seg = monophonic_reduce(intv3, False)
    v1 = snap_segments_to_grid(mel_seg, six, end_tick)
    v2 = snap_segments_to_grid(bas_seg, six, end_tick)
    if sum(d for _, d in v1) != sum(d for _, d in v2):
        raise SystemExit("melody/bass 16th totals mismatch")

    vbl_per_16th = (60.0 / 4.0 / SCORE_BPM) * VBL_HZ
    pair_set: dict[tuple[int, int], int] = {}

    def pair_idx(midi: int, d16: int) -> int:
        freq = 0 if midi == 0 else midi_to_gb_freq_reg(midi)
        dv = max(1, min(255, int(round(d16 * vbl_per_16th))))
        key = (freq, dv)
        if key not in pair_set:
            if len(pair_set) >= 255:
                raise SystemExit("more than 254 unique (freq,dur) pairs — reduce score length or quantize")
            pair_set[key] = len(pair_set)
        return pair_set[key]

    mel_idx = [pair_idx(m, d) for m, d in v1]
    bas_idx = [pair_idx(m, d) for m, d in v2]
    mel_pre, bas_pre = prelude_indices(v1, v2)
    dict_list = sorted(pair_set.items(), key=lambda x: x[1])

    lines = [
        "/* Auto-generated by tools/emit_bwv527_from_mid.py — do not edit by hand */",
        "#pragma bank 5",
        "#include <stdint.h>",
        "#include <gb/gb.h>",
        '#include "bwv527_music.h"',
        "",
        "BANKREF(bwv527_music)",
        "",
        f"const GBNote bwv527_dict[{len(dict_list)}] = {{",
    ]
    for (freq, dur), _ in dict_list:
        lines.append(f"    {{ {freq}u, {dur}u }},")
    lines.append("};")
    lines.append("")

    def emit_idx(name: str, idxs: list[int]) -> None:
        lines.append(f"const uint8_t {name}[] = {{")
        row: list[str] = []
        for i in idxs:
            row.append(f"{i}u")
            if len(row) == 20:
                lines.append("    " + ", ".join(row) + ",")
                row = []
        row.append(f"{SENTINEL_IDX}u")
        if row:
            lines.append("    " + ", ".join(row) + ",")
        lines.append("};")
        lines.append("")

    emit_idx("bwv527_melody", mel_idx)
    emit_idx("bwv527_bass", bas_idx)
    SRC_C.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if not SRC_H.is_file():
        SRC_H.write_text(
            "\n".join(
                [
                    "#ifndef BWV527_MUSIC_H",
                    "#define BWV527_MUSIC_H",
                    "",
                    "#include <stdint.h>",
                    "",
                    "typedef struct {",
                    "    uint16_t freq;",
                    "    uint8_t  dur;",
                    "} GBNote;",
                    "",
                    "extern const GBNote bwv527_dict[];",
                    "extern const uint8_t bwv527_melody[];",
                    "extern const uint8_t bwv527_bass[];",
                    "",
                    "#define BWV527_DICT_LEN 1u",
                    "#define BWV527_SENTINEL 0xFFu",
                    "",
                    "// Filled by tools/emit_bwv527_from_mid.py",
                    "#define BWV527_NUM_MELODY_EVENTS 1u",
                    "#define BWV527_NUM_BASS_EVENTS 1u",
                    "#define BWV527_PRELUDE_END_MELODY 0u",
                    "#define BWV527_PRELUDE_END_BASS 0u",
                    "#define BWV527_GAME_START_MELODY BWV527_PRELUDE_END_MELODY",
                    "#define BWV527_GAME_START_BASS BWV527_PRELUDE_END_BASS",
                    "",
                    "#endif",
                    "",
                ]
            ),
            encoding="utf-8",
        )

    h_text = SRC_H.read_text(encoding="utf-8")
    replacements = {
        "BWV527_DICT_LEN": f"{len(dict_list)}u",
        "BWV527_NUM_MELODY_EVENTS": f"{len(mel_idx) + 1}u",
        "BWV527_NUM_BASS_EVENTS": f"{len(bas_idx) + 1}u",
        "BWV527_PRELUDE_END_MELODY": f"{mel_pre}u",
        "BWV527_PRELUDE_END_BASS": f"{bas_pre}u",
    }
    for macro, val in replacements.items():
        h_text = re.sub(rf"#define {macro} \S+", f"#define {macro} {val}", h_text, count=1)
    SRC_H.write_text(h_text, encoding="utf-8")

    data_bytes = len(dict_list) * 3 + len(mel_idx) + 1 + len(bas_idx) + 1
    print(f"Wrote {SRC_C}: dict={len(dict_list)} entries, mel={len(mel_idx)}, bas={len(bas_idx)}")
    print(f"Total data: {data_bytes} bytes")
    print(f"Prelude ({PRELUDE_FRAC:.0%}): mel_i={mel_pre} bas_i={bas_pre} (SCORE_BPM={SCORE_BPM})")


if __name__ == "__main__":
    main()
