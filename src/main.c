/* GBC roguelike: drunkard's walk, walls '#', floor '.', pits '0', enemy snakes 'S'/'s'.
 * Turn-based: player moves 1 tile then all snakes move 1 tile; 400ms delay between turns. */
#include <gb/gb.h>        // GBDK core (display, joypad, etc.)
#include <gb/cgb.h>       // Color Game Boy palettes
#include <gbdk/console.h> // gotoxy, setchar, printf
#include <gbdk/font.h>    // font_init, font_load
#include <rand.h>         // initrand, rand, randw
#include <stdint.h>       // uint8_t, uint16_t
#include <stdio.h>        // printf

#define GRID_W 20           // dungeon width in tiles
#define GRID_H 17           // dungeon height
#define MAP_SIZE (GRID_W * GRID_H)  // total tiles (macro)
#define UI_ROW 0            // top row for FLR + life bar
#define DUNGEON_ROW(y) ((y) + 1)  // map rows start below UI
#define WALK_STEPS 350       // drunkard steps (more = bigger caves)
#define NUM_PITS 6		// number of pits per level
#define MAX_SNAKES 8		// max snakes per level
#define NUM_SNAKES 5		// snakes per level
#define TURN_DELAY_MS 300	// delay between turns
#define SNAKE_ANIM_FRAMES 30   // flip S/s every 30 frames (~0.5s at 60fps)

#define TILE_WALL  0   // wall tile id (logical)
#define TILE_FLOOR 1   // floor tile id
#define TILE_PIT   2   // pit tile id

/* Tileset indices (from res/tileset.png); VRAM offset so font stays in 0–127 */
#define TILESET_VRAM_OFFSET 128
// Row 0 (0–15)
#define TILE_TEST       0
#define TILE_WALL_1     1
#define TILE_MUSH_1     2
#define TILE_TORCH_1    3
#define TILE_TORCH_2    4
#define TILE_SKULL_1    5
#define TILE_CLASS_1    6
// Row 1 (16–31)
#define TILE_GROUND_1   16
#define TILE_WALL_2     17
#define TILE_MUSH_2     18
#define TILE_PILLAR_1   19
#define TILE_CHEST_1    20
#define TILE_BARREL_1   21
#define TILE_CLASS_2    22
#define TILE_ENEMY_1    23
#define TILE_ENEMY_2    24
#define TILE_ENEMY_3    25
// Row 2 (32–47)
#define TILE_DOOR_1     33
#define TILE_PILLAR_2   35
#define TILE_CLASS_3    38

#define START_X (GRID_W / 2)  // player spawn column
#define START_Y (GRID_H / 2)  // player spawn row

static uint8_t map[MAP_SIZE];      // tile id per cell
static uint16_t level_seed = 12345;  // RNG seed for this level

static uint8_t snake_x[MAX_SNAKES];  // snake column per slot
static uint8_t snake_y[MAX_SNAKES];  // snake row per slot
static uint8_t snake_type[MAX_SNAKES]; /* 0 = 'S', 1 = 's' */
static uint8_t snake_hp[MAX_SNAKES];   /* S = 2 HP, s = 1 HP */
static uint8_t num_snakes;  // count of active snakes
#define MAX_CORPSES MAX_SNAKES  // reuse snake slot count
#define PAL_CORPSE 7  // CGB palette index for corpse (0-7 only; was 8)
static uint8_t corpse_x[MAX_CORPSES];  // corpse column
static uint8_t corpse_y[MAX_CORPSES];  // corpse row
static uint8_t num_corpses;  // count of corpses on level
static uint8_t snake_anim_counter;  // frames until next S/s flip
static uint8_t snake_anim_toggle;   // 0 = normal case, 1 = swapped
static uint8_t wall_tileset_index = TILE_WALL_2;  // current wall tile (tileset-local index; debug-cyclable)
static uint8_t wall_palette_index = 3;  // current wall palette index (debug-cyclable)

#define SNAKE_DEAD 255       // sentinel: snake slot unused
static uint8_t floor_num = 1;  // current dungeon floor
static uint8_t player_hp = 10;  // player hit points

#define PLAYER_HP_MAX 10  // full HP
#define LIFE_BAR_LEN 5       // number of segments in bar
#define SNAKE_DAMAGE 1   // damage per snake hit
#define PAL_UI 6   // CGB palette for UI text
#define PAL_LIFE_UI 5   // CGB palette for life bar fill (freed 7 for corpse)

/* CGB palettes: 0 default, 1 snake, 2 player, 3 wall, 4 pit, 5 life_ui, 6 UI, 7 corpse */
static const palette_color_t pal_default[] = {
	RGB(0, 0, 31), RGB(0, 0, 31), RGB(0, 0, 31), RGB(31, 31, 31)  // 4 shades (CGB tile)
};
static const palette_color_t pal_ui[] = {
	RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(31, 31, 31)  // black bg, white text
};
static const palette_color_t pal_snake[] = {
	RGB(0, 0, 31), RGB(0, 31, 0), RGB(0, 28, 0), RGB(0, 24, 0)  // green gradient
};
static const palette_color_t pal_corpse[] = {
	RGB(0, 0, 31), RGB(0, 8, 0), RGB(0, 5, 0), RGB(0, 3, 0)     // dark green
};
static const palette_color_t pal_player[] = {
	RGB(0, 0, 31), RGB(31, 31, 31), RGB(24, 24, 24), RGB(18, 18, 18)  // silver
};
static const palette_color_t pal_wall[] = {
	RGB(0, 0, 31), RGB(12, 12, 12), RGB(8, 8, 8), RGB(5, 5, 5)  // gray
};
static const palette_color_t pal_pit[] = {
	RGB(0, 0, 31), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0)     // black
};
static const palette_color_t pal_life[] = {
	RGB(0, 0, 31), RGB(31, 0, 0), RGB(31, 4, 4), RGB(28, 0, 0)  // red
};
/* Life bar on UI row: black bg + red (so bar sits on black) */
static const palette_color_t pal_life_ui[] = {
	RGB(0, 0, 0), RGB(31, 0, 0), RGB(31, 4, 4), RGB(28, 0, 0)  // black + red
};

static inline uint8_t tile_at(uint8_t x, uint8_t y) {
	return map[(uint16_t)y * GRID_W + x];  // row-major index
}

static inline void set_tile(uint8_t x, uint8_t y, uint8_t t) {
	map[(uint16_t)y * GRID_W + x] = t;  // write tile at row-major index
}

#define TILESET_NTILES 26  // 416 bytes / 16 per tile
extern const uint8_t tileset_tiles[];

static char tile_char(uint8_t t) {
	if (t == TILE_WALL) return '#';  // fallback if not using tileset
	if (t == TILE_PIT) return '0';   // pit glyph
	return '.';  // TILE_FLOOR
}

/* VRAM tile index for map tile type (walls use tileset; floor/pit stay as font). */
static uint8_t tile_vram_index(uint8_t t) {
	if (t == TILE_WALL) return (uint8_t)(TILESET_VRAM_OFFSET + wall_tileset_index);
	return 0;  // caller uses setchar for non-wall
}

static uint8_t tile_palette(uint8_t t) {
	if (t == TILE_WALL) return wall_palette_index;  // debug: wall palette is cyclable
	if (t == TILE_PIT) return 4;   // pal_pit
	return 0;  // default
}

static uint8_t corpse_at(uint8_t x, uint8_t y) {
	uint8_t i;
	for (i = 0; i < num_corpses; i++)
		if (corpse_x[i] == x && corpse_y[i] == y) return 1;
	return 0;  // no corpse here
}

/* Draw map/corpse at (x,y) to screen (no player or live snake) */
static void draw_cell(uint8_t x, uint8_t y) {
	uint8_t sy = DUNGEON_ROW(y);
	gotoxy(x, sy);
	if (corpse_at(x, y)) {
		setchar('x');
		set_bkg_attribute_xy(x, sy, PAL_CORPSE);
	} else {
		uint8_t t = tile_at(x, y);
		uint8_t vram = tile_vram_index(t);
		if (vram) {
			set_bkg_tiles(x, sy, 1, 1, &vram);
			set_bkg_attribute_xy(x, sy, tile_palette(t));
		} else {
			setchar(tile_char(t));
			set_bkg_attribute_xy(x, sy, tile_palette(t));
		}
	}
}

/* Returns snake index at (x,y) or 255 if none (skip dead) */
static uint8_t snake_at(uint8_t x, uint8_t y) {
	uint8_t i;
	for (i = 0; i < num_snakes; i++)
		if (snake_x[i] != SNAKE_DEAD && snake_x[i] == x && snake_y[i] == y) return i;
	return 255;  // no snake here
}

static uint8_t is_walkable(uint8_t x, uint8_t y) {
	uint8_t t = tile_at(x, y);
	return (t == TILE_FLOOR || t == TILE_PIT);  // floor or pit only
}

/* Drunkard's walk: start full of walls, walk from center carving floor. */
static void generate_level(void) {
	uint16_t i;
	uint8_t x = START_X, y = START_Y;  // walker starts at center
	for (i = 0; i < MAP_SIZE; i++) map[i] = TILE_WALL;  // fill with walls

	initrand(level_seed);  // seed RNG for this level
	set_tile(x, y, TILE_FLOOR);  // carve start cell

	for (i = 0; i < WALK_STEPS; i++) {
		uint8_t d = rand() & 3;   // 0..3 = left/right/up/down
		uint8_t nx = x, ny = y;
		if (d == 0) nx = x > 1 ? x - 1 : x;   // left, keep 1-cell border
		else if (d == 1) nx = x < GRID_W - 2 ? x + 1 : x;  // right
		else if (d == 2) ny = y > 1 ? y - 1 : y;   // up
		else ny = y < GRID_H - 2 ? y + 1 : y;  // down
		set_tile(nx, ny, TILE_FLOOR);
		x = nx;
		y = ny;
	}

	/* Place pits on random floor tiles, never on start */
	uint8_t placed = 0;
	for (uint8_t attempts = 0; attempts < 200 && placed < NUM_PITS; attempts++) {
		uint8_t tx = (uint8_t)(rand() % GRID_W);
		uint8_t ty = (uint8_t)(rand() % GRID_H);
		if ((tx != START_X || ty != START_Y) && tile_at(tx, ty) == TILE_FLOOR) {
			set_tile(tx, ty, TILE_PIT);
			placed++;
		}
	}
}

/* Spawn snakes on random floor tiles (not start, not pit, no overlap). */
static void spawn_snakes(void) {
	uint8_t i;
	num_snakes = 0;
	for (i = 0; i < NUM_SNAKES; i++) {
		uint8_t attempts;
		for (attempts = 0; attempts < 100; attempts++) {
			uint8_t tx = (uint8_t)(rand() % GRID_W);
			uint8_t ty = (uint8_t)(rand() % GRID_H);
			if ((tx != START_X || ty != START_Y) && is_walkable(tx, ty) && snake_at(tx, ty) == 255) {
				snake_x[num_snakes] = tx;
				snake_y[num_snakes] = ty;
				snake_type[num_snakes] = (uint8_t)(rand() & 1);  // 0=S, 1=s
				snake_hp[num_snakes] = snake_type[num_snakes] ? 1 : 2;  /* s=1, S=2 */
				num_snakes++;
				break;  // found valid spot
			}
		}
	}
}

/* Draw full screen: UI row (FLR + life bar), then map/snakes/player with per-tile palette. */
static void draw_screen(uint8_t px, uint8_t py) {
	uint8_t x, y;
	gotoxy(0, UI_ROW);
	printf("FLR:%02d", floor_num);  // floor number
	for (x = 0; x < 6; x++) set_bkg_attribute_xy(x, UI_ROW, PAL_UI);  // style "FLR:01"
	gotoxy(6, UI_ROW);
	setchar(' ');  // gap before life bar
	set_bkg_attribute_xy(6, UI_ROW, PAL_UI);
	gotoxy(7, UI_ROW);
	setchar('L');  // "L" for life
	set_bkg_attribute_xy(7, UI_ROW, PAL_LIFE_UI); //question which pal for red? answer
	gotoxy(8, UI_ROW);
	setchar('[');  // bar left bracket
	set_bkg_attribute_xy(8, UI_ROW, PAL_UI);
	/* 5 segments, deplete right-to-left: left=0–20%, right=80–100% */
	{
		uint8_t k;
		uint8_t pct = (uint16_t)player_hp * 100 / PLAYER_HP_MAX;  // HP as 0..100
		for (k = 0; k < LIFE_BAR_LEN; k++) {
			uint8_t thresh_full = (uint8_t)(20 * (k + 1));   // segment k full above this %
			uint8_t thresh_half = (uint8_t)(20 * k + 10);   // half below this
			char c;
			uint8_t pal;
			if (pct >= thresh_full) { c = '='; pal = PAL_LIFE_UI; }  // full segment
			else if (pct >= thresh_half) { c = '-'; pal = PAL_LIFE_UI; }  // half
			else { c = '_'; pal = PAL_UI; }  // empty
			gotoxy(9 + k, UI_ROW);
			setchar(c);
			set_bkg_attribute_xy(9 + k, UI_ROW, pal);
		}
	}
	gotoxy(9 + LIFE_BAR_LEN, UI_ROW);
	setchar(']');  // bar right bracket
	set_bkg_attribute_xy(9 + LIFE_BAR_LEN, UI_ROW, PAL_UI);
	gotoxy(10 + LIFE_BAR_LEN, UI_ROW);
	printf("%3d%%", (uint16_t)player_hp * 100 / PLAYER_HP_MAX);  // HP percent
	for (x = 10 + LIFE_BAR_LEN; x < 20; x++) set_bkg_attribute_xy(x, UI_ROW, PAL_UI);  // rest of row

	for (y = 0; y < GRID_H; y++)
		for (x = 0; x < GRID_W; x++) {
			uint8_t sy = DUNGEON_ROW(y);
			gotoxy(x, sy);
			if (x == px && y == py) {
				setchar('@');  // player
				set_bkg_attribute_xy(x, sy, 2);  // pal_player
			} else if (snake_at(x, y) != 255) {
				uint8_t si = snake_at(x, y);
				uint8_t is_big = (snake_type[si] == 0);
				setchar(is_big ? (snake_anim_toggle ? 's' : 'S') : (snake_anim_toggle ? 'S' : 's'));
				set_bkg_attribute_xy(x, sy, is_big ? 2 : 1);  // big = gold/yellow (pal_player), small = green
			} else if (corpse_at(x, y)) {
				setchar('x');  // corpse glyph
				set_bkg_attribute_xy(x, sy, PAL_CORPSE);
			} else {
				uint8_t t = tile_at(x, y);
				uint8_t vram = tile_vram_index(t);
				if (vram) {
					set_bkg_tiles(x, sy, 1, 1, &vram);
					set_bkg_attribute_xy(x, sy, tile_palette(t));
				} else {
					setchar(tile_char(t));
					set_bkg_attribute_xy(x, sy, tile_palette(t));
				}
			}
		}
}

/* Redraw only snake tiles (for animation); uses current snake_anim_toggle. */
static void draw_snake_cells(uint8_t px, uint8_t py) {
	uint8_t i;
	for (i = 0; i < num_snakes; i++) {
		if (snake_x[i] == SNAKE_DEAD) continue;
		uint8_t sx = snake_x[i], sy = snake_y[i];
		if (sx == px && sy == py) continue;  // player on top
		gotoxy(sx, DUNGEON_ROW(sy));
		uint8_t is_big = (snake_type[i] == 0);
		setchar(is_big ? (snake_anim_toggle ? 's' : 'S') : (snake_anim_toggle ? 'S' : 's'));
		set_bkg_attribute_xy(sx, DUNGEON_ROW(sy), is_big ? 2 : 1);
	}
}

/* Jitter scroll for a few frames when player takes damage */
static void screen_shake(void) {
	uint8_t f;
	const int8_t off[] = { 2, -2, -1, 1, -2, 2, 1, -1 };  // 8-frame offset pattern
	for (f = 0; f < 8; f++) {
		SCX_REG = (uint8_t)off[f];  // scroll X
		SCY_REG = (uint8_t)off[(f + 2) & 7];  // scroll Y (phase-shifted)
		wait_vbl_done();  // one frame
	}
	SCX_REG = 0;
	SCY_REG = 0;  // restore no scroll
}

/* Move each snake 1 tile toward player; apply SNAKE_DAMAGE if one hits. Snake does not move onto player. Return 2=dead, 1=hit, 0=ok. */
static uint8_t move_snakes(uint8_t px, uint8_t py) {
	uint8_t i;
	for (i = 0; i < num_snakes; i++) {
		if (snake_x[i] == SNAKE_DEAD) continue;  // skip dead slot
		int8_t dx = 0, dy = 0;  // direction toward player
		uint8_t sx = snake_x[i], sy = snake_y[i];
		if (sx < px) dx = 1;
		else if (sx > px) dx = -1;
		if (sy < py) dy = 1;
		else if (sy > py) dy = -1;
		uint8_t nx = sx, ny = sy;  // candidate new position
		if ((uint8_t)(px > sx ? px - sx : sx - px) >= (uint8_t)(py > sy ? py - sy : sy - py)) {
			if (dx) nx = (uint8_t)((int16_t)sx + dx);  // prefer horizontal if tie
		} else {
			if (dy) ny = (uint8_t)((int16_t)sy + dy);  // else vertical
		}
		if (nx == sx && ny == sy) continue;  // no move
		if (snake_at(nx, ny) != 255) continue;  // blocked by other snake
		if (nx == px && ny == py) {
			if (player_hp > SNAKE_DAMAGE) player_hp -= SNAKE_DAMAGE;
			else player_hp = 0;
			return player_hp == 0 ? 2 : 1;  // 2=dead, 1=hit
		}
		if (!is_walkable(nx, ny) || snake_at(nx, ny) != 255) continue;
		snake_x[i] = nx;
		snake_y[i] = ny;
		if (tile_at(nx, ny) == TILE_PIT) snake_x[i] = SNAKE_DEAD;  // snake falls in pit and is removed
	}
	return 0;  // no hit
}

static void enter_level(uint8_t *px, uint8_t *py, uint8_t from_pit) {
	if (from_pit) floor_num++;  // next floor
	else { floor_num = 1; player_hp = PLAYER_HP_MAX; }  // new game: reset floor and HP
	num_corpses = 0;  // clear corpses
	snake_anim_counter = 0;
	snake_anim_toggle = 0;
	wall_tileset_index = TILE_WALL_2; // reset debug wall tile per level
	wall_palette_index = 3; // reset wall palette per level (default pal_wall)
	level_seed = randw();  // new seed per level
	generate_level();
	spawn_snakes();
	draw_screen(*px = START_X, *py = START_Y);  // set player to start and draw
}

static void game_over_screen(void) {
	uint8_t x, y;
	for (y = 0; y < 18; y++)
		for (x = 0; x < GRID_W; x++) {
			gotoxy(x, y);
			setchar(' ');  // clear screen
		}
	gotoxy(6, 8);
	printf("GAME OVER");  // title
	gotoxy(5, 10);
	printf("START=again");  // prompt
	while (1) {
		if (joypad() & J_START) break;  // wait for START
		wait_vbl_done();  // burn frame
	}
}

int main(void) {
	DISPLAY_OFF;  // avoid garbage during init
	set_default_palette();  // CGB default palette
	set_bkg_palette(0, 1, pal_default);  // palette 0
	set_bkg_palette(1, 1, pal_snake);
	set_bkg_palette(2, 1, pal_player);
	set_bkg_palette(3, 1, pal_wall);
	set_bkg_palette(4, 1, pal_pit);
	set_bkg_palette(5, 1, pal_life_ui);  // 5 = life bar (red)
	set_bkg_palette(6, 1, pal_ui);
	set_bkg_palette(7, 1, pal_corpse);   // 7 = corpse (was 8; CGB only has 0-7)

	font_init();
	font_load(font_ibm);  // font stays in low VRAM tiles
	font_color(3, 0);
	set_bkg_data(TILESET_VRAM_OFFSET, TILESET_NTILES, tileset_tiles);  // dungeon tiles above font

	initrand(12345);
	uint8_t px, py;
	uint8_t prev_j = 0;
	enter_level(&px, &py, 0);  // 0 = new game, not from pit

	SHOW_BKG;  // show background layer
	DISPLAY_ON;  // turn on LCD
	enable_interrupts();  // allow VBL etc.

	while (1) {
		uint8_t j = joypad();  // read input
		uint8_t nx = px, ny = py;  // candidate new position
		if (j & J_LEFT)  nx = px > 0 ? (uint8_t)(px - 1) : px;
		if (j & J_RIGHT) nx = px < GRID_W - 1 ? (uint8_t)(px + 1) : px;
		if (j & J_UP)    ny = py > 0 ? (uint8_t)(py - 1) : py;
		if (j & J_DOWN)  ny = py < GRID_H - 1 ? (uint8_t)(py + 1) : py;

		/* Debug: cycle wall tileset index on SELECT press edge (no turn advance) */
		if ((j & J_SELECT) && !(prev_j & J_SELECT)) {
			wall_tileset_index++;
			if (wall_tileset_index >= TILESET_NTILES) wall_tileset_index = 0;
			draw_screen(px, py);  // redraw map with new wall tile
		}

		/* Debug: cycle wall palette on A press edge (no turn advance) */
		if ((j & J_A) && !(prev_j & J_A)) {
			wall_palette_index = (uint8_t)((wall_palette_index + 1) & 7); // palettes 0-7
			draw_screen(px, py);  // redraw map with new palette
		}

		if (nx != px || ny != py) {  // player moved
			uint8_t si = snake_at(nx, ny);
			if (si != 255) {
				/* Player attacks snake in place (does not move onto tile); 1 damage, S=2hp s=1hp */
				if (snake_hp[si] > 1) snake_hp[si]--;  // deal 1 damage
				else {
					if (num_corpses < MAX_CORPSES) {
						corpse_x[num_corpses] = snake_x[si];
						corpse_y[num_corpses] = snake_y[si];
						num_corpses++;
					}
					snake_x[si] = SNAKE_DEAD;  // mark slot dead
				}
				draw_screen(px, py);  // redraw after attack
				{
					uint8_t result = move_snakes(px, py);  // snakes respond
					if (result == 1 || result == 2) screen_shake();
					draw_screen(px, py);
					if (result == 2) {
						game_over_screen();
						enter_level(&px, &py, 0);
						continue;
					}
				}
				delay(TURN_DELAY_MS);  // turn delay
			} else {
				uint8_t t = tile_at(nx, ny);
				if (t == TILE_WALL) { /* no-op */ }  // can't move into wall
				else if (t == TILE_PIT) {
					draw_cell(px, py);  // erase old player cell before level change
					enter_level(&px, &py, 1);  // 1 = from pit (next floor)
				} else {
					draw_cell(px, py);  // erase old position
					px = nx;
					py = ny;
					gotoxy(px, DUNGEON_ROW(py));
					setchar('@');  // draw player at new cell
					set_bkg_attribute_xy(px, DUNGEON_ROW(py), 2);
					/* Snakes move 1 tile per turn; 1 damage on hit, game over at 0 HP */
					{
						uint8_t result = move_snakes(px, py);
						if (result == 1 || result == 2) screen_shake();
						draw_screen(px, py);  // redraw after snake moves
						if (result == 2) {
							game_over_screen();
							enter_level(&px, &py, 0);
							continue;
						}
					}
					delay(TURN_DELAY_MS);  // turn delay
				}
			}
		}
		wait_vbl_done();  // sync to vblank
		snake_anim_counter++;
		if (snake_anim_counter >= SNAKE_ANIM_FRAMES) {
			snake_anim_counter = 0;
			snake_anim_toggle ^= 1;
			draw_snake_cells(px, py);
		}
		prev_j = j;
	}
}
