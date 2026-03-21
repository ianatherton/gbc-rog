// Core game entrypoint and global state.
#include "defs.h"
#include "map.h"
#include "enemy.h"
#include "render.h"
#include "ui.h"

#define SCROLL_SPEED 2   // pixels per frame while moving

/* ── Global game state ───────────────────────────────────────────────────── */
uint8_t  player_hp  = PLAYER_HP_MAX;
uint8_t  floor_num  = 1;
uint16_t run_seed   = 12345;
uint16_t camera_px  = 0;
uint16_t camera_py  = 0;

/* ── Camera ──────────────────────────────────────────────────────────────── */

static void scroll_toward(uint8_t target_tx, uint8_t target_ty, uint8_t px, uint8_t py) {
    uint16_t target_px = (uint16_t)target_tx * 8u;
    uint16_t target_py = (uint16_t)target_ty * 8u;

    while (camera_px != target_px || camera_py != target_py) {
        uint8_t old_col = (uint8_t)((camera_px >> 3) + GRID_W);
        uint8_t old_row = (uint8_t)((camera_py >> 3) + GRID_H);

        if (camera_px < target_px) { camera_px += SCROLL_SPEED; if (camera_px > target_px) camera_px = target_px; }
        else if (camera_px > target_px) { camera_px -= SCROLL_SPEED; if (camera_px < target_px) camera_px = target_px; }
        if (camera_py < target_py) { camera_py += SCROLL_SPEED; if (camera_py > target_py) camera_py = target_py; }
        else if (camera_py > target_py) { camera_py -= SCROLL_SPEED; if (camera_py < target_py) camera_py = target_py; }

        wait_vbl_done();
        SCX_REG = (uint8_t)(camera_px & 0xFFu);
        SCY_REG = (uint8_t)((camera_py - 8u) & 0xFFu);

        uint8_t new_col = (uint8_t)((camera_px >> 3) + GRID_W);
        uint8_t new_row = (uint8_t)((camera_py >> 3) + GRID_H);
        if (new_col != old_col) draw_col_strip(new_col, px, py);
        if (new_row != old_row) draw_row_strip(new_row, px, py);
        draw_ui_rows();  // redraw UI into current ring slots so they stay fixed
    }
}

/* ── Level entry ─────────────────────────────────────────────────────────── */

static void enter_level(uint8_t *px, uint8_t *py, uint8_t from_pit) {
    if (from_pit) {
        floor_num++;
    } else {
        floor_num = 1;
        player_hp = PLAYER_HP_MAX;
    }
    // Derive a unique seed per floor from the fixed run seed + floor number.
    // floor_num differentiates floors; multiply+XOR scrambles bits so adjacent floors don't look like slight shifts of each other.
    uint16_t floor_seed = (uint16_t)(run_seed * 2053u)
                        ^ (uint16_t)(floor_num * 6364u)
                        ^ 0xACE1u;
    if (!floor_seed) floor_seed = 0xACE1u;

    num_corpses       = 0;
    enemy_anim_toggle = 0;
    enemy_anim_reset();
    wall_tileset_index = TILE_WALL_1;
    wall_palette_index = 3;
    initrand(floor_seed);
    generate_level();
    spawn_enemies();
    *px = START_X;
    *py = START_Y;
    {
        int16_t cx = (int16_t)*px - GRID_W / 2;
        int16_t cy = (int16_t)*py - GRID_H / 2;
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > (int16_t)(MAP_W - GRID_W)) cx = (int16_t)(MAP_W - GRID_W);
        if (cy > (int16_t)(MAP_H - GRID_H)) cy = (int16_t)(MAP_H - GRID_H);
        camera_px = (uint16_t)(uint8_t)cx * 8u;
        camera_py = (uint16_t)(uint8_t)cy * 8u;
    }
    wait_vbl_done();
    draw_screen(*px, *py);
}

/* ── New run ─────────────────────────────────────────────────────────────── */

static void start_new_run(uint8_t *px, uint8_t *py, uint8_t *prev_j,
                           uint16_t *run_entropy) {
    *run_entropy += 1u + (uint16_t)DIV_REG;
    uint16_t seed = title_screen(*run_entropy);
    if (!seed) seed = (uint16_t)(*run_entropy ^ 0xACE1u);
    if (!seed) seed = 0xACE1u;

    run_seed  = seed;
    floor_num = 0;
    *prev_j   = 0;
    enter_level(px, py, 0);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void) {
    DISPLAY_OFF;
    set_default_palette();
    load_palettes();
    font_init();
    font_load(font_ibm);
    font_color(3, 0);
    set_bkg_data(TILESET_VRAM_OFFSET, TILESET_NTILES, tileset_tiles);
    SCX_REG = 0;
    SCY_REG = 0;
    SHOW_BKG;
    DISPLAY_ON;
    enable_interrupts();

    // 16-bit boot entropy (DIV is 8-bit; read twice so init timing varies the high byte)
    uint16_t power_on_ticks = (uint16_t)DIV_REG | ((uint16_t)DIV_REG << 8);
    static uint16_t run_entropy;
    run_entropy = power_on_ticks;
    uint8_t px, py, prev_j = 0;
    start_new_run(&px, &py, &prev_j, &run_entropy);

    while (1) {
        uint8_t j  = joypad();
        uint8_t nx = px, ny = py;

        if (j & J_LEFT)  nx = px > 0         ? (uint8_t)(px-1) : px;
        if (j & J_RIGHT) nx = px < MAP_W-1   ? (uint8_t)(px+1) : px;
        if (j & J_UP)    ny = py > 0         ? (uint8_t)(py-1) : py;
        if (j & J_DOWN)  ny = py < MAP_H-1   ? (uint8_t)(py+1) : py;

        if ((j & J_SELECT) && !(prev_j & J_SELECT)) {
            if (++wall_tileset_index >= TILESET_NTILES) wall_tileset_index = 0;
            wait_vbl_done();
            draw_screen(px, py);
        }
        if ((j & J_A) && !(prev_j & J_A)) {
            wall_palette_index = (uint8_t)((wall_palette_index+1) & 7);
            wait_vbl_done();
            draw_screen(px, py);
        }

        if (nx != px || ny != py) {
            uint8_t ei = enemy_at(nx, ny);
            uint8_t result = 0;

            if (ei != ENEMY_DEAD) {
                if (enemy_hp[ei] > 1) {
                    enemy_hp[ei]--;
                } else {
                    if (num_corpses < MAX_CORPSES) {
                        corpse_x[num_corpses] = enemy_x[ei];
                        corpse_y[num_corpses] = enemy_y[ei];
                        num_corpses++;
                    }
                    enemy_x[ei] = ENEMY_DEAD;
                }

                result = move_enemies(px, py);
                if (result == 1 || result == 2) screen_shake();
                wait_vbl_done();
                draw_screen(px, py);
                if (result == 2) {
                    game_over_screen();
                    start_new_run(&px, &py, &prev_j, &run_entropy);
                    continue;
                }
                delay(TURN_DELAY_MS);
            } else {
                uint8_t t = tile_at(nx, ny);
                if (t == TILE_WALL) {
                    /* bump; no turn */
                } else if (t == TILE_PIT) {
                    wait_vbl_done();
                    draw_cell(px, py, px, py);
                    enter_level(&px, &py, 1);
                } else {
                    wait_vbl_done();
                    draw_cell(px, py, px, py);
                    px = nx;
                    py = ny;
                    {
                        uint8_t target_cx = (px > GRID_W / 2) ? (uint8_t)(px - GRID_W / 2) : 0;
                        uint8_t target_cy = (py > GRID_H / 2) ? (uint8_t)(py - GRID_H / 2) : 0;
                        if (target_cx > (uint8_t)(MAP_W - GRID_W)) target_cx = (uint8_t)(MAP_W - GRID_W);
                        if (target_cy > (uint8_t)(MAP_H - GRID_H)) target_cy = (uint8_t)(MAP_H - GRID_H);
                        scroll_toward(target_cx, target_cy, px, py);
                    }
                    result = move_enemies(px, py);
                    if (result == 1 || result == 2) screen_shake();
                    wait_vbl_done();
                    draw_screen(px, py);

                    if (result == 2) {
                        game_over_screen();
                        start_new_run(&px, &py, &prev_j, &run_entropy);
                        continue;
                    }
                    delay(TURN_DELAY_MS);
                }
            }
        }

        if (enemy_anim_update()) {
            wait_vbl_done();
            draw_enemy_cells(px, py);
        }
        prev_j = j;
    }
}
