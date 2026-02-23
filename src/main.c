/* GBC roguelike: blue background, player as @ using GBDK built-in font, D-pad move. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>

#define GRID_W 20
#define GRID_H 18

/* CGB palette 0: blue background (0), white character (3) */
static const palette_color_t bg_palette[] = {
	RGB(0, 0, 31),   /* 0: blue */
	RGB(0, 0, 31),
	RGB(0, 0, 31),
	RGB(31, 31, 31)  /* 3: white for letter */
};

int main(void) {
	DISPLAY_OFF;
	set_default_palette();
	set_bkg_palette(0, 1, bg_palette);

	font_init();
	font_load(font_ibm);
	font_color(3, 0);  /* foreground white, background blue */

	/* Fill grid with spaces (blue) */
	for (uint8_t y = 0; y < GRID_H; y++)
		for (uint8_t x = 0; x < GRID_W; x++) {
			gotoxy(x, y);
			setchar(' ');
		}

	uint8_t px = GRID_W / 2, py = GRID_H / 2;
	gotoxy(px, py);
	setchar('@');

	SHOW_BKG;
	DISPLAY_ON;
	enable_interrupts();

	while (1) {
		uint8_t j = joypad();
		uint8_t nx = px, ny = py;
		if (j & J_LEFT)  nx = px > 0 ? px - 1 : px;
		if (j & J_RIGHT) nx = px < GRID_W - 1 ? px + 1 : px;
		if (j & J_UP)    ny = py > 0 ? py - 1 : py;
		if (j & J_DOWN)  ny = py < GRID_H - 1 ? py + 1 : py;
		if (nx != px || ny != py) {
			gotoxy(px, py);
			setchar(' ');
			px = nx; py = ny;
			gotoxy(px, py);
			setchar('@');
		}
		wait_vbl_done();
	}
}
