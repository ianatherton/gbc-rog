#include <gb/cgb.h>
#include "wall_palettes.h"

// Four colors each: index 0–3 often read as shadow → highlight on the wall tile; hues are *mixed* per ramp (not monochrome).
const palette_color_t wall_palette_table[NUM_WALL_PALETTES][4] = {
    { RGB(2,2,6),   RGB(8,6,4),   RGB(14,12,10), RGB(22,20,18) }, // 0 cool shadow / warm stone / neutral light
    { RGB(0,4,2),   RGB(8,10,0),  RGB(4,18,8),   RGB(24,22,12) }, // 1 green-black → olive → bright green → yellow cream
    { RGB(2,0,6),   RGB(4,12,4),  RGB(14,10,2),  RGB(10,24,16) }, // 2 violet shadow / moss / bark / seafoam light
    { RGB(4,2,0),   RGB(2,8,8),   RGB(16,14,4),  RGB(26,20,10) }, // 3 rust dark / teal smear / gold mid / peach highlight
    { RGB(6,0,8),   RGB(0,8,16),  RGB(12,16,24), RGB(26,24,28) }, // 4 purple void / deep cyan / icy blue / pink-white
    { RGB(0,2,8),   RGB(8,6,2),   RGB(4,16,18),  RGB(20,26,20) }, // 5 navy / umber / teal / pale green-white
    { RGB(8,0,4),   RGB(4,4,14),  RGB(18,8,20),  RGB(28,16,22) }, // 6 wine shadow / blue / magenta / salmon light
    { RGB(2,4,8),   RGB(12,0,12), RGB(20,10,8),   RGB(30,22,14) }, // 7 slate blue / purple / terracotta / sand
    { RGB(4,0,0),   RGB(18,4,0),  RGB(8,14,4),   RGB(28,24,8) }, // 8 black-red / orange rust / green lichen / yellow hot
    { RGB(6,2,8),   RGB(16,6,0),  RGB(24,14,4),  RGB(30,20,18) }, // 9 plum / amber / gold / rose-white
    { RGB(0,6,4),   RGB(10,4,8),  RGB(22,16,2),  RGB(26,26,20) }, // 10 teal-black / violet-brown / gold / ivory
    { RGB(4,4,2),   RGB(14,8,4),  RGB(6,14,18),  RGB(24,22,16) }, // 11 mud / copper / cyan oxide / cream
    { RGB(6,2,6),   RGB(12,10,4), RGB(8,8,18),     RGB(28,24,20) }, // 12 mauve shadow / sepia / periwinkle / warm white
    { RGB(2,6,6),   RGB(10,10,10),RGB(18,12,16), RGB(26,22,26) }, // 13 teal-gray / neutral / rose-gray / lilac-white
    { RGB(2,0,2),   RGB(12,2,4),  RGB(18,10,2),  RGB(24,18,14) }, // 14 purple-black / blood / rust / bone yellow
    { RGB(0,8,0),   RGB(8,4,12),  RGB(4,20,8),   RGB(22,28,12) }, // 15 green / indigo mold / neon green / lime-white
    { RGB(4,6,8),   RGB(2,14,6),  RGB(16,20,4),  RGB(28,26,22) }, // 16 blue-gray / mint / chartreuse / peach white
    { RGB(8,0,6),   RGB(4,8,14),  RGB(20,12,18), RGB(30,22,28) }, // 17 magenta-black / steel blue / mauve / pink white
    { RGB(4,0,4),   RGB(14,4,8),  RGB(8,18,12),  RGB(26,20,24) }, // 18 plum / brick / aqua mid / lavender-white
    { RGB(0,4,10),  RGB(10,10,0), RGB(6,20,20),  RGB(24,26,30) }, // 19 deep blue / olive / turquoise / icy pink
    { RGB(2,2,2),   RGB(0,12,14), RGB(16,8,16),  RGB(26,20,12) }, // 20 near-black / cyan / purple / gold light
    { RGB(0,6,8),   RGB(12,6,2),  RGB(8,14,22),  RGB(30,24,16) }, // 21 teal shadow / brown / sky / butter
    { RGB(8,4,0),   RGB(2,8,12),  RGB(20,14,6),  RGB(18,22,28) }, // 22 ochre / ocean blue / tan / pale blue-white
    { RGB(6,0,2),   RGB(16,8,4),  RGB(10,6,20),  RGB(28,22,18) }, // 23 maroon / clay / indigo streak / apricot
    { RGB(2,6,2),   RGB(10,2,8),  RGB(6,16,10),  RGB(24,26,14) }, // 24 green shadow / purple rot / jungle / yellow fog
    { RGB(4,0,8),   RGB(8,12,4),  RGB(18,10,14), RGB(22,24,28) }, // 25 indigo / moss / dusty rose / ice
    { RGB(0,2,4),   RGB(12,4,10), RGB(4,18,8),   RGB(28,26,24) }, // 26 blue-black / violet / spring green / shell
    { RGB(8,2,0),   RGB(4,6,14),  RGB(22,12,8),  RGB(26,24,30) }, // 27 ember / blue soot / rust / blue-white spark
    { RGB(6,4,8),   RGB(14,10,2), RGB(8,16,20),  RGB(30,22,14) }, // 28 dusty purple / gold dirt / aqua / peach
    { RGB(2,8,4),   RGB(12,0,6),  RGB(20,16,4),  RGB(24,28,22) }, // 29 swamp green / burgundy / brass / mint-white
    { RGB(4,2,2),   RGB(6,10,18), RGB(18,14,8),  RGB(22,20,26) }, // 30 brown-black / steel blue / tan / lavender mist
    { RGB(10,0,8),  RGB(4,14,6),  RGB(22,8,16),  RGB(30,26,20) }, // 31 hot magenta-black / mint / fuchsia mid / cream
    { RGB(6,6,0),   RGB(2,6,16),  RGB(18,20,6),  RGB(28,14,22) }, // 32 olive black / blue mold / yellow-green / orchid
    { RGB(8,6,4),   RGB(4,4,12),  RGB(20,18,8),  RGB(26,22,30) }, // 33 tan / indigo shadow / wheat / periwinkle light
    { RGB(0,0,6),   RGB(10,8,2),  RGB(6,16,24),  RGB(30,26,22) }, // 34 blue void / brown silt / cyan / warm white
    { RGB(2,10,6),  RGB(12,4,12), RGB(8,20,14),  RGB(26,24,30) }, // 35 jade shadow / violet / emerald / pink ice
    { RGB(6,4,0),   RGB(0,10,8),  RGB(22,18,4),  RGB(20,26,28) }, // 36 bronze / teal / gold / pale cyan
    { RGB(4,2,10),  RGB(14,6,4),  RGB(6,14,22),  RGB(28,24,18) }, // 37 indigo / rust / sky blue / sand highlight
    { RGB(8,0,2),   RGB(2,12,8),  RGB(22,6,6),   RGB(26,22,26) }, // 38 red-black / teal / dusty red / lilac haze
    { RGB(0,4,6),   RGB(14,0,14), RGB(10,18,10), RGB(30,24,16) }, // 39 cyan-black / purple / sage / gold-white
};
