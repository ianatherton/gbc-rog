#!/usr/bin/env python3
"""Normalize Krita art exports for the png2asset build. Idempotent — run after every export.

Krita sheets are AUTHORED IN INVERTED TONES: what the artist draws white must end up black
in the build asset, #AAA <-> #555, etc. (the game's palettes / gorgon bitplane swap / sphinx
palette were all authored against the inverted result). Every sheet the build loads goes
through the same pipeline:

  1. sanity guards: refuse a fully-transparent or single-flat-color image (a bad Krita
     export — building one would silently bake a blank sheet into the ROM);
  2. snap every opaque pixel to the nearest of the 4 tones (white/#AAA/#555/black), so
     stray anti-aliased colors can't leak into png2asset's palettes;
  3. INVERT each tone (255-c: white<->black, #AAA<->#555) — only for raw Krita exports,
     detected by their transparent pixels carrying (0,0,0,0). Already-processed files
     (indexed, or RGBA with white-transparent) are recognized and NOT inverted again,
     so re-running the script is always safe;
  4. save in the format that sheet's png2asset rule expects.

Sheets:
- res/bosses.png  -> 4-color INDEXED, palette order [white, #AAA, #555, black] (required
  by -keep_palette_order). Transparent -> index 0 (= OBJ-transparent); a visible white
  (i.e. black in the Krita drawing) would render as a hole, so it snaps to #AAA instead.
- res/tileset.png -> RGBA (its rule auto-derives palettes). Transparent pixels are
  normalized to (255,255,255,0): the RGB under alpha=0 matters to png2asset's palette
  derivation, and white-transparent is what the known-good sheets used.

Derived tiles (generated here, DO NOT hand-draw them in Krita — they get overwritten):
- tileset M1 = M2 rotated 90 deg CW (vertical HUD border rail; the GBC can only flip,
  never rotate, so the vertical rail needs its own tile).

Then runs make.
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

WHITE, LIGHT, DARK, BLACK = (255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)
TONES = [WHITE, LIGHT, DARK, BLACK]


def load_pixels(path):
    """Return (rgba_pixel_list, is_raw_export). Already-processed files load with index 0 /
    white-transparent restored to transparency and must NOT be inverted again."""
    im = Image.open(path)
    if im.mode == "P":  # our own indexed output: index 0 is the transparent slot
        pal = im.getpalette()
        tones = [tuple(pal[i * 3:i * 3 + 3]) for i in range(4)]
        return im.size, [(0, 0, 0, 0) if v == 0 else tones[v] + (255,) for v in im.getdata()], False
    data = list(im.convert("RGBA").getdata())
    transparent = [px for px in data if px[3] < 128]
    if not transparent:
        sys.exit(f"ERROR: {path} has no transparent pixels — can't tell a raw Krita export "
                 f"from a processed sheet, refusing to guess. File left untouched.")
    is_raw = any(px[:3] != WHITE for px in transparent)  # Krita writes (0,0,0,0); we write white-transparent
    return im.size, data, is_raw


def normalize(path, out_indexed, forbid_visible_white):
    size, pixels, is_raw = load_pixels(path)

    opaque = [px for px in pixels if px[3] >= 128]
    if not opaque:
        sys.exit(f"ERROR: {path} is fully transparent — bad Krita export? File left untouched.")
    if len({px[:3] for px in opaque}) < 2:
        sys.exit(f"ERROR: {path} has a single flat color — bad Krita export? File left untouched.")

    snapped = []
    for px in pixels:
        if px[3] < 128:
            snapped.append((255, 255, 255, 0) if not out_indexed else (0, 0, 0, 0))
            continue
        tone = min(TONES, key=lambda t: abs(t[0] - (px[0] + px[1] + px[2]) // 3))
        if is_raw:
            tone = tuple(255 - c for c in tone)  # authored inverted: white<->black, #AAA<->#555
        if forbid_visible_white and tone == WHITE:
            tone = LIGHT  # indexed sheets render index 0 (white) as a hole — never emit it visibly
        snapped.append(tone + (255,))

    if out_indexed:
        # Palette order is load-bearing: 2bpp color N = palette index N in every tile.
        # Exactly 4 PLTE entries — a padded 256-entry palette reads as "64 palettes" to png2asset.
        index_of = {(0, 0, 0, 0): 0, LIGHT + (255,): 1, DARK + (255,): 2, BLACK + (255,): 3}
        out = Image.new("P", size, 0)
        out.putpalette([c for tone in TONES for c in tone])
        out.putdata([index_of[px] for px in snapped])
        out.save(path, optimize=False)
        state = "inverted + indexed" if is_raw else "re-indexed (already processed)"
    else:
        out = Image.new("RGBA", size)
        out.putdata(snapped)
        out.save(path)
        state = "inverted + snapped to RGBA" if is_raw else "re-snapped (already processed)"
    print(f"{path}: {state}")


def sheet_cell_origin(col, row):
    """Sheet cell name (col letter A=0.., 1-based row) -> top-left pixel of its 8x8 tile."""
    return col * 8, (row - 1) * 8


def derive_rotated_tiles(path):
    """Rotate M2 (horizontal HUD border rail) 90 deg CW into M1 (vertical rail).

    The GBC can only X/Y-flip a tile, never rotate it, so the vertical rail needs its own
    tile. M1 is blank and unreferenced, so we generate it here instead of hand-drawing it.
    Runs AFTER normalize() so it operates on final (already inverted/snapped) tones.
    Idempotent: always sources from M2, and M2 is never written."""
    im = Image.open(path).convert("RGBA")
    sx, sy = sheet_cell_origin(12, 2)   # M2 - source, horizontal
    dx, dy = sheet_cell_origin(12, 1)   # M1 - destination, vertical
    src = [[im.getpixel((sx + x, sy + y)) for x in range(8)] for y in range(8)]
    for y in range(8):
        for x in range(8):
            im.putpixel((dx + x, dy + y), src[7 - x][y])  # rot 90 CW
    im.save(path)
    print(f"{path}: M1 <- rot90cw(M2)")


def main():
    normalize(ROOT / "res" / "bosses.png", out_indexed=True, forbid_visible_white=True)
    normalize(ROOT / "res" / "tileset.png", out_indexed=False, forbid_visible_white=False)
    derive_rotated_tiles(ROOT / "res" / "tileset.png")
    print("running make ...")
    sys.exit(subprocess.call(["make"], cwd=ROOT))


if __name__ == "__main__":
    main()
