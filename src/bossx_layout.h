#ifndef BOSSX_LAYOUT_H
#define BOSSX_LAYOUT_H

/* ── Boss-prototype tile layouts (single source of truth) ─────────────────────────────────────
   Each entry is BOSSX_TILE(src, rx, ry):
     src   = bosses_tiles[] index of the art cell: (sheet_row-1)*16 + (sheet_col-1), cols a..p.
     rx,ry = tile offset from the boss anchor. The anchor is the enemy's logical tile =
             BOTTOM-LEFT of the body (matching gorgon/sphinx), so ry is 0 on the ground row
             and negative going up. The 2-tile collision footprint is anchor + (1,0).
   Tile order is load-bearing: tile i is uploaded into bossx_pool[i] (BOSSX_POOL_INIT, defs.h)
   by biome_bossx.c (bank 24) and drawn from the same index by entity_sprites.c (bank 17) —
   each file re-expands these lists with its own BOSSX_TILE definition, so the two banks can
   never drift. Frame-2 / attack / projectile cells in the sheet (hydra mouth+fireball+moat,
   mara hands+heart's eye frames, skelking sword, dragon mouth, spider webs) are deliberately
   NOT listed — prototypes are single-frame; behavior art comes with each boss's real AI.

   HYDRA — 2-wide head d1:e1 (face in e1) over neck d2 (fireball f1 + moat d3:f4 reserved). */
#define BOSSX_HYDRA_TILES \
    BOSSX_TILE(19, 0, 0) \
    BOSSX_TILE( 3, 0, -1) BOSSX_TILE( 4, 1, -1)

/* DEMON — 3x3 body d5:f7 plus one arm tile each side on the middle row (c6, g6). */
#define BOSSX_DEMON_TILES \
    BOSSX_TILE( 67, 0, -2) BOSSX_TILE( 68, 1, -2) BOSSX_TILE( 69, 2, -2) \
    BOSSX_TILE( 82, -1, -1) BOSSX_TILE( 83, 0, -1) BOSSX_TILE( 84, 1, -1) \
    BOSSX_TILE( 85, 2, -1) BOSSX_TILE( 86, 3, -1) \
    BOSSX_TILE( 99, 0, 0) BOSSX_TILE(100, 1, 0) BOSSX_TILE(101, 2, 0)

/* GIANT SPIDER — 3x3, g1:i3 (webs j2 / k1:m3 are future floor props, not part of the body). */
#define BOSSX_GSPIDER_TILES \
    BOSSX_TILE( 6, 0, -2) BOSSX_TILE( 7, 1, -2) BOSSX_TILE( 8, 2, -2) \
    BOSSX_TILE(22, 0, -1) BOSSX_TILE(23, 1, -1) BOSSX_TILE(24, 2, -1) \
    BOSSX_TILE(38, 0, 0) BOSSX_TILE(39, 1, 0) BOSSX_TILE(40, 2, 0)

/* MARA EYE — 3x2 winged eye h5:j6 (attack k5:m6 and hurt h7:j8 frames reserved for later). */
#define BOSSX_MARAEYE_TILES \
    BOSSX_TILE(71, 0, -1) BOSSX_TILE(72, 1, -1) BOSSX_TILE(73, 2, -1) \
    BOSSX_TILE(87, 0, 0) BOSSX_TILE(88, 1, 0) BOSSX_TILE(89, 2, 0)

/* SKELETON KING — 2x5 body j12:k16 plus arm tiles i14/l14 (sword g13:h14 reserved for later). */
#define BOSSX_SKELKING_TILES \
    BOSSX_TILE(185, 0, -4) BOSSX_TILE(186, 1, -4) \
    BOSSX_TILE(201, 0, -3) BOSSX_TILE(202, 1, -3) \
    BOSSX_TILE(216, -1, -2) BOSSX_TILE(217, 0, -2) BOSSX_TILE(218, 1, -2) BOSSX_TILE(219, 2, -2) \
    BOSSX_TILE(233, 0, -1) BOSSX_TILE(234, 1, -1) \
    BOSSX_TILE(249, 0, 0) BOSSX_TILE(250, 1, 0)

/* DRAGON — 2x6 body m11:n16 with a 2x3 tail o14:p16 hanging off the lower right
   (mouth-open l11:l12 reserved for later). Biggest boss: all 18 pool slots. */
#define BOSSX_DRAGON_TILES \
    BOSSX_TILE(172, 0, -5) BOSSX_TILE(173, 1, -5) \
    BOSSX_TILE(188, 0, -4) BOSSX_TILE(189, 1, -4) \
    BOSSX_TILE(204, 0, -3) BOSSX_TILE(205, 1, -3) \
    BOSSX_TILE(220, 0, -2) BOSSX_TILE(221, 1, -2) BOSSX_TILE(222, 2, -2) BOSSX_TILE(223, 3, -2) \
    BOSSX_TILE(236, 0, -1) BOSSX_TILE(237, 1, -1) BOSSX_TILE(238, 2, -1) BOSSX_TILE(239, 3, -1) \
    BOSSX_TILE(252, 0, 0) BOSSX_TILE(253, 1, 0) BOSSX_TILE(254, 2, 0) BOSSX_TILE(255, 3, 0)

/* MARA — the 4x4 heart a12:d15 (hand frames c10:f11/g10:h11 + fingers join in the real fight). */
#define BOSSX_MARA_TILES \
    BOSSX_TILE(176, 0, -3) BOSSX_TILE(177, 1, -3) BOSSX_TILE(178, 2, -3) BOSSX_TILE(179, 3, -3) \
    BOSSX_TILE(192, 0, -2) BOSSX_TILE(193, 1, -2) BOSSX_TILE(194, 2, -2) BOSSX_TILE(195, 3, -2) \
    BOSSX_TILE(208, 0, -1) BOSSX_TILE(209, 1, -1) BOSSX_TILE(210, 2, -1) BOSSX_TILE(211, 3, -1) \
    BOSSX_TILE(224, 0, 0) BOSSX_TILE(225, 1, 0) BOSSX_TILE(226, 2, 0) BOSSX_TILE(227, 3, 0)

#endif // BOSSX_LAYOUT_H
