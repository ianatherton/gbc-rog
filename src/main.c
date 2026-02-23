/* GBC roguelike: drunkard's walk, walls '#', floor '.', pits '0', enemy snakes 'S'/'s'.
 * Turn-based: player moves 1 tile then all snakes move 1 tile; 400ms delay between turns. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <rand.h>
#include <stdint.h>
#include <stdio.h>

#define GRID_W 20
#define GRID_H 17
#define MAP_SIZE (GRID_W * GRID_H)
#define UI_ROW 0
#define DUNGEON_ROW(y) ((y) + 1)
#define WALK_STEPS 350
#define NUM_PITS 6
#define MAX_SNAKES 8
#define NUM_SNAKES 5
#define TURN_DELAY_MS 300

#define TILE_WALL  0
#define TILE_FLOOR 1
#define TILE_PIT   2

#define START_X (GRID_W / 2)
#define START_Y (GRID_H / 2)

static uint8_t map[MAP_SIZE];
static uint16_t level_seed = 12345;

static uint8_t snake_x[MAX_SNAKES];
static uint8_t snake_y[MAX_SNAKES];
static uint8_t snake_type[MAX_SNAKES]; /* 0 = 'S', 1 = 's' */
static uint8_t snake_hp[MAX_SNAKES];   /* S = 2 HP, s = 1 HP */
static uint8_t num_snakes;
#define MAX_CORPSES MAX_SNAKES
#define PAL_CORPSE 8
static uint8_t corpse_x[MAX_CORPSES];
static uint8_t corpse_y[MAX_CORPSES];
static uint8_t num_corpses;

#define SNAKE_DEAD 255
static uint8_t floor_num = 1;
static uint8_t player_hp = 10;

#define PLAYER_HP_MAX 10
#define LIFE_BAR_LEN 5
#define SNAKE_DAMAGE 1
#define PAL_UI 6
#define PAL_LIFE_UI 7

/* CGB palettes: 0 default, 1 snake, 2 player, 3 wall, 4 pit, 5 life red, 6 UI, 7 life_ui, 8 corpse */
static const palette_color_t pal_default[] = {
	RGB(0, 0, 31), RGB(0, 0, 31), RGB(0, 0, 31), RGB(31, 31, 31)
};
static const palette_color_t pal_ui[] = {
	RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(31, 31, 31)
};
static const palette_color_t pal_snake[] = {
	RGB(0, 0, 31), RGB(0, 31, 0), RGB(0, 28, 0), RGB(0, 24, 0)
};
static const palette_color_t pal_corpse[] = {
	RGB(0, 0, 31), RGB(0, 8, 0), RGB(0, 5, 0), RGB(0, 3, 0)
};
static const palette_color_t pal_player[] = {
	RGB(0, 0, 31), RGB(31, 28, 0), RGB(25, 22, 0), RGB(20, 18, 0)
};
static const palette_color_t pal_wall[] = {
	RGB(0, 0, 31), RGB(12, 12, 12), RGB(8, 8, 8), RGB(5, 5, 5)
};
static const palette_color_t pal_pit[] = {
	RGB(0, 0, 31), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0)
};
static const palette_color_t pal_life[] = {
	RGB(0, 0, 31), RGB(31, 0, 0), RGB(31, 4, 4), RGB(28, 0, 0)
};
/* Life bar on UI row: black bg + red (so bar sits on black) */
static const palette_color_t pal_life_ui[] = {
	RGB(0, 0, 0), RGB(31, 0, 0), RGB(31, 4, 4), RGB(28, 0, 0)
};

static inline uint8_t tile_at(uint8_t x, uint8_t y) {
	return map[(uint16_t)y * GRID_W + x];
}

static inline void set_tile(uint8_t x, uint8_t y, uint8_t t) {
	map[(uint16_t)y * GRID_W + x] = t;
}

static char tile_char(uint8_t t) {
	if (t == TILE_WALL) return '#';
	if (t == TILE_PIT) return '0';
	return '.';
}

static uint8_t tile_palette(uint8_t t) {
	if (t == TILE_WALL) return 3;
	if (t == TILE_PIT) return 4;
	return 0;
}

static uint8_t corpse_at(uint8_t x, uint8_t y) {
	uint8_t i;
	for (i = 0; i < num_corpses; i++)
		if (corpse_x[i] == x && corpse_y[i] == y) return 1;
	return 0;
}

/* Draw map/corpse at (x,y) to screen (no player or live snake) */
static void draw_cell(uint8_t x, uint8_t y) {
	uint8_t sy = DUNGEON_ROW(y);
	gotoxy(x, sy);
	if (corpse_at(x, y)) {
		setchar('@');
		set_bkg_attribute_xy(x, sy, PAL_CORPSE);
	} else {
		uint8_t t = tile_at(x, y);
		setchar(tile_char(t));
		set_bkg_attribute_xy(x, sy, tile_palette(t));
	}
}

/* Returns snake index at (x,y) or 255 if none (skip dead) */
static uint8_t snake_at(uint8_t x, uint8_t y) {
	uint8_t i;
	for (i = 0; i < num_snakes; i++)
		if (snake_x[i] != SNAKE_DEAD && snake_x[i] == x && snake_y[i] == y) return i;
	return 255;
}

static uint8_t is_walkable(uint8_t x, uint8_t y) {
	uint8_t t = tile_at(x, y);
	return (t == TILE_FLOOR || t == TILE_PIT);
}

/* Drunkard's walk: start full of walls, walk from center carving floor. */
static void generate_level(void) {
	uint16_t i;
	uint8_t x = START_X, y = START_Y;
	for (i = 0; i < MAP_SIZE; i++) map[i] = TILE_WALL;

	initrand(level_seed);
	set_tile(x, y, TILE_FLOOR);

	for (i = 0; i < WALK_STEPS; i++) {
		uint8_t d = rand() & 3;
		uint8_t nx = x, ny = y;
		if (d == 0) nx = x > 1 ? x - 1 : x;
		else if (d == 1) nx = x < GRID_W - 2 ? x + 1 : x;
		else if (d == 2) ny = y > 1 ? y - 1 : y;
		else ny = y < GRID_H - 2 ? y + 1 : y;
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
				snake_type[num_snakes] = (uint8_t)(rand() & 1);
				snake_hp[num_snakes] = snake_type[num_snakes] ? 1 : 2;  /* s=1, S=2 */
				num_snakes++;
				break;
			}
		}
	}
}

/* Draw full screen: UI row (FLR + life bar), then map/snakes/player with per-tile palette. */
static void draw_screen(uint8_t px, uint8_t py) {
	uint8_t x, y;
	gotoxy(0, UI_ROW);
	printf("FLR:%02d", floor_num);
	for (x = 0; x < 6; x++) set_bkg_attribute_xy(x, UI_ROW, PAL_UI);
	gotoxy(6, UI_ROW);
	setchar(' ');
	set_bkg_attribute_xy(6, UI_ROW, PAL_UI);
	gotoxy(7, UI_ROW);
	setchar('L');
	set_bkg_attribute_xy(7, UI_ROW, PAL_UI);
	gotoxy(8, UI_ROW);
	setchar('[');
	set_bkg_attribute_xy(8, UI_ROW, PAL_UI);
	/* 5 segments, deplete right-to-left: left=0–20%, right=80–100% */
	{
		uint8_t k;
		uint8_t pct = (uint16_t)player_hp * 100 / PLAYER_HP_MAX;
		for (k = 0; k < LIFE_BAR_LEN; k++) {
			uint8_t thresh_full = (uint8_t)(20 * (k + 1));
			uint8_t thresh_half = (uint8_t)(20 * k + 10);
			char c;
			uint8_t pal;
			if (pct >= thresh_full) { c = '='; pal = PAL_LIFE_UI; }
			else if (pct >= thresh_half) { c = '-'; pal = PAL_LIFE_UI; }
			else { c = '_'; pal = PAL_UI; }
			gotoxy(9 + k, UI_ROW);
			setchar(c);
			set_bkg_attribute_xy(9 + k, UI_ROW, pal);
		}
	}
	gotoxy(9 + LIFE_BAR_LEN, UI_ROW);
	setchar(']');
	set_bkg_attribute_xy(9 + LIFE_BAR_LEN, UI_ROW, PAL_UI);
	gotoxy(10 + LIFE_BAR_LEN, UI_ROW);
	printf("%3d%%", (uint16_t)player_hp * 100 / PLAYER_HP_MAX);
	for (x = 10 + LIFE_BAR_LEN; x < 20; x++) set_bkg_attribute_xy(x, UI_ROW, PAL_UI);

	for (y = 0; y < GRID_H; y++)
		for (x = 0; x < GRID_W; x++) {
			uint8_t sy = DUNGEON_ROW(y);
			gotoxy(x, sy);
			if (x == px && y == py) {
				setchar('@');
				set_bkg_attribute_xy(x, sy, 2);
			} else if (snake_at(x, y) != 255) {
				uint8_t si = snake_at(x, y);
				setchar(snake_type[si] ? 's' : 'S');
				set_bkg_attribute_xy(x, sy, 1);
			} else if (corpse_at(x, y)) {
				setchar('@');
				set_bkg_attribute_xy(x, sy, PAL_CORPSE);
			} else {
				uint8_t t = tile_at(x, y);
				setchar(tile_char(t));
				set_bkg_attribute_xy(x, sy, tile_palette(t));
			}
		}
}

/* Jitter scroll for a few frames when player takes damage */
static void screen_shake(void) {
	uint8_t f;
	const int8_t off[] = { 2, -2, -1, 1, -2, 2, 1, -1 };
	for (f = 0; f < 8; f++) {
		SCX_REG = (uint8_t)off[f];
		SCY_REG = (uint8_t)off[(f + 2) & 7];
		wait_vbl_done();
	}
	SCX_REG = 0;
	SCY_REG = 0;
}

/* Move each snake 1 tile toward player; apply SNAKE_DAMAGE if one hits. Snake does not move onto player. Return 2=dead, 1=hit, 0=ok. */
static uint8_t move_snakes(uint8_t px, uint8_t py) {
	uint8_t i;
	for (i = 0; i < num_snakes; i++) {
		if (snake_x[i] == SNAKE_DEAD) continue;
		int8_t dx = 0, dy = 0;
		uint8_t sx = snake_x[i], sy = snake_y[i];
		if (sx < px) dx = 1;
		else if (sx > px) dx = -1;
		if (sy < py) dy = 1;
		else if (sy > py) dy = -1;
		uint8_t nx = sx, ny = sy;
		if ((uint8_t)(px > sx ? px - sx : sx - px) >= (uint8_t)(py > sy ? py - sy : sy - py)) {
			if (dx) nx = (uint8_t)((int16_t)sx + dx);
		} else {
			if (dy) ny = (uint8_t)((int16_t)sy + dy);
		}
		if (nx == sx && ny == sy) continue;
		if (snake_at(nx, ny) != 255) continue;
		if (nx == px && ny == py) {
			if (player_hp > SNAKE_DAMAGE) player_hp -= SNAKE_DAMAGE;
			else player_hp = 0;
			return player_hp == 0 ? 2 : 1;
		}
		if (!is_walkable(nx, ny) || snake_at(nx, ny) != 255) continue;
		snake_x[i] = nx;
		snake_y[i] = ny;
	}
	return 0;
}

static void enter_level(uint8_t *px, uint8_t *py, uint8_t from_pit) {
	if (from_pit) floor_num++;
	else { floor_num = 1; player_hp = PLAYER_HP_MAX; }
	num_corpses = 0;
	level_seed = randw();
	generate_level();
	spawn_snakes();
	draw_screen(*px = START_X, *py = START_Y);
}

static void game_over_screen(void) {
	uint8_t x, y;
	for (y = 0; y < 18; y++)
		for (x = 0; x < GRID_W; x++) {
			gotoxy(x, y);
			setchar(' ');
		}
	gotoxy(6, 8);
	printf("GAME OVER");
	gotoxy(5, 10);
	printf("START=again");
	while (1) {
		if (joypad() & J_START) break;
		wait_vbl_done();
	}
}

int main(void) {
	DISPLAY_OFF;
	set_default_palette();
	set_bkg_palette(0, 1, pal_default);
	set_bkg_palette(1, 1, pal_snake);
	set_bkg_palette(2, 1, pal_player);
	set_bkg_palette(3, 1, pal_wall);
	set_bkg_palette(4, 1, pal_pit);
	set_bkg_palette(5, 1, pal_life);
	set_bkg_palette(6, 1, pal_ui);
	set_bkg_palette(7, 1, pal_life_ui);
	set_bkg_palette(8, 1, pal_corpse);

	font_init();
	font_load(font_ibm);
	font_color(3, 0);

	initrand(12345);
	uint8_t px, py;
	enter_level(&px, &py, 0);

	SHOW_BKG;
	DISPLAY_ON;
	enable_interrupts();

	while (1) {
		uint8_t j = joypad();
		uint8_t nx = px, ny = py;
		if (j & J_LEFT)  nx = px > 0 ? (uint8_t)(px - 1) : px;
		if (j & J_RIGHT) nx = px < GRID_W - 1 ? (uint8_t)(px + 1) : px;
		if (j & J_UP)    ny = py > 0 ? (uint8_t)(py - 1) : py;
		if (j & J_DOWN)  ny = py < GRID_H - 1 ? (uint8_t)(py + 1) : py;

		if (nx != px || ny != py) {
			uint8_t si = snake_at(nx, ny);
			if (si != 255) {
				/* Player attacks snake in place (does not move onto tile); 1 damage, S=2hp s=1hp */
				if (snake_hp[si] > 1) snake_hp[si]--;
				else {
					if (num_corpses < MAX_CORPSES) {
						corpse_x[num_corpses] = snake_x[si];
						corpse_y[num_corpses] = snake_y[si];
						num_corpses++;
					}
					snake_x[si] = SNAKE_DEAD;
				}
				draw_screen(px, py);
				{
					uint8_t result = move_snakes(px, py);
					if (result == 1 || result == 2) screen_shake();
					draw_screen(px, py);
					if (result == 2) {
						game_over_screen();
						enter_level(&px, &py, 0);
						continue;
					}
				}
				delay(TURN_DELAY_MS);
			} else {
				uint8_t t = tile_at(nx, ny);
				if (t == TILE_WALL) { /* no-op */ }
				else if (t == TILE_PIT) {
					draw_cell(px, py);
					enter_level(&px, &py, 1);
				} else {
					draw_cell(px, py);
					px = nx;
					py = ny;
					gotoxy(px, DUNGEON_ROW(py));
					setchar('@');
					set_bkg_attribute_xy(px, DUNGEON_ROW(py), 2);
					/* Snakes move 1 tile per turn; 1 damage on hit, game over at 0 HP */
					{
						uint8_t result = move_snakes(px, py);
						if (result == 1 || result == 2) screen_shake();
						draw_screen(px, py);
						if (result == 2) {
							game_over_screen();
							enter_level(&px, &py, 0);
							continue;
						}
					}
					delay(TURN_DELAY_MS);
				}
			}
		}
		wait_vbl_done();
	}
}
