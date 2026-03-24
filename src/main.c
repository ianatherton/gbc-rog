#include "defs.h"   // shared types, limits, tile IDs, extern globals
#include "map.h"    // generate_level, tile_at, camera-related tile drawing hooks
#include "enemy.h"  // spawn_enemies, move_enemies, enemy_at, anim
#include "render.h" // draw_screen, strips, shake, palettes
#include "entity_sprites.h" // OAM actors, walk sync, attack lunge
#include "ui.h"     // title_screen, game_over_screen
#include "lcd.h"    // raster HUD / viewport scroll split
#include "music.h"  // background theme
#include "wall_palettes.h" // NUM_WALL_PALETTES

#define SCROLL_SPEED 2 // px/frame; 2 = 4-frame glide per tile for smooth visible interpolation

uint8_t  player_hp  = PLAYER_HP_BASE_MAX; // remaining HP; reset on new run, not on pit descent
uint8_t  player_hp_max = PLAYER_HP_BASE_MAX; // runtime max HP; +10 per level
uint8_t  player_level = 1;                 // XP needed scales: 15,20,25,30,...
uint8_t  player_damage = 1;                // bump attack damage; +1 per level
uint16_t player_xp  = 0;                   // XP progress inside current level
uint8_t  floor_num  = 1;             // 1-based floor index; incremented when taking a pit
uint16_t run_seed   = 12345;         // default until title picks a seed; drives per-floor RNG
uint16_t camera_px  = 0;             // viewport top-left in background pixels (sub-tile scroll)
uint16_t camera_py  = 0;             // viewport Y; line-8 ISR uses SCY=camera_py (dungeon ring is +1 row vs HUD)

static void grant_xp_from_kill(uint8_t enemy_damage) {
    uint16_t next_level_xp;
    player_xp = (uint16_t)(player_xp + enemy_damage);
    while (1) {
        next_level_xp = (uint16_t)PLAYER_LEVEL_XP_BASE + (uint16_t)(player_level - 1u) * PLAYER_LEVEL_XP_STEP;
        if (player_xp < next_level_xp) break;
        player_xp = (uint16_t)(player_xp - next_level_xp);
        if (player_level < 255u) player_level++;
        if (player_damage < 255u) player_damage++;
        if (player_hp_max <= 245u) player_hp_max = (uint8_t)(player_hp_max + 10u);
        else player_hp_max = 255u;
        player_hp = player_hp_max; // full heal on every level-up
    }
}

static void player_glide_when_camera_idle(uint8_t opx, uint8_t opy, uint8_t px, uint8_t py) { // map edge: camera fixed, sprite still eases one tile
    int16_t wx = (int16_t)opx * 8, wy = (int16_t)opy * 8;
    int16_t ex = (int16_t)px * 8, ey = (int16_t)py * 8;
    while (wx != ex || wy != ey) {
        if (wx < ex) { wx += SCROLL_SPEED; if (wx > ex) wx = ex; }
        else if (wx > ex) { if ((wx - ex) <= SCROLL_SPEED) wx = ex; else wx -= SCROLL_SPEED; }
        if (wy < ey) { wy += SCROLL_SPEED; if (wy > ey) wy = ey; }
        else if (wy > ey) { if ((wy - ey) <= SCROLL_SPEED) wy = ey; else wy -= SCROLL_SPEED; }
        entity_sprites_set_player_world(wx, wy);
        entity_sprites_refresh(px, py);
        wait_vbl_done();
    }
    entity_sprites_clear_player_world();
    entity_sprites_refresh(px, py);
}

static void scroll_toward(uint8_t target_tx, uint8_t target_ty,
                          uint8_t opx, uint8_t opy, uint8_t px, uint8_t py) { // smooth pan; sprite eases from old tile with camera
    uint16_t target_px = (uint16_t)target_tx * 8u;
    uint16_t target_py = (uint16_t)target_ty * 8u;
    uint16_t start_cam_px = camera_px, start_cam_py = camera_py;
    uint16_t guard_steps = 0u;

    if (camera_px == target_px && camera_py == target_py && (opx != px || opy != py)) {
        player_glide_when_camera_idle(opx, opy, px, py);
        return;
    }

    while (camera_px != target_px || camera_py != target_py) {
        if (++guard_steps > 2048u) { camera_px = target_px; camera_py = target_py; entity_sprites_clear_player_world(); entity_sprites_refresh(px, py); break; }
        uint8_t old_ctx = (uint8_t)(camera_px >> 3);
        uint8_t old_cty = (uint8_t)(camera_py >> 3);

        if (camera_px < target_px) { camera_px += SCROLL_SPEED; if (camera_px > target_px) camera_px = target_px; }
        else if (camera_px > target_px) {
            if ((camera_px - target_px) <= SCROLL_SPEED) camera_px = target_px;
            else camera_px -= SCROLL_SPEED;
        }
        if (camera_py < target_py) { camera_py += SCROLL_SPEED; if (camera_py > target_py) camera_py = target_py; }
        else if (camera_py > target_py) {
            if ((camera_py - target_py) <= SCROLL_SPEED) camera_py = target_py;
            else camera_py -= SCROLL_SPEED;
        }

        {
            int16_t pwx = (int16_t)px * 8, pwy = (int16_t)py * 8;
            if (target_px != start_cam_px) {
                int32_t num = (int32_t)((int16_t)px * 8 - (int16_t)opx * 8)
                            * (int32_t)((int32_t)camera_px - (int32_t)start_cam_px);
                int32_t den = (int32_t)target_px - (int32_t)start_cam_px;
                if (den != 0) pwx = (int16_t)opx * 8 + (int16_t)(num / den);
            } else pwx = (int16_t)px * 8;
            if (target_py != start_cam_py) {
                int32_t num = (int32_t)((int16_t)py * 8 - (int16_t)opy * 8)
                            * (int32_t)((int32_t)camera_py - (int32_t)start_cam_py);
                int32_t den = (int32_t)target_py - (int32_t)start_cam_py;
                if (den != 0) pwy = (int16_t)opy * 8 + (int16_t)(num / den);
            } else pwy = (int16_t)py * 8;
            entity_sprites_set_player_world(pwx, pwy);
        }

        wait_vbl_done();

        {
            uint8_t new_ctx = (uint8_t)(camera_px >> 3);
            uint8_t new_cty = (uint8_t)(camera_py >> 3);
            if (new_ctx != old_ctx) // right → draw leading right edge; left → draw leading left edge
                draw_col_strip(new_ctx > old_ctx ? (uint8_t)(new_ctx + GRID_W) : new_ctx, px, py);
            if (new_cty != old_cty)
                draw_row_strip(new_cty > old_cty ? (uint8_t)(new_cty + GRID_H) : new_cty, px, py);
        }
        draw_ui_rows();
        entity_sprites_refresh(px, py);
    }
    entity_sprites_clear_player_world();
    entity_sprites_refresh(px, py);
}

static void enter_level(uint8_t *px, uint8_t *py, uint8_t from_pit) { // load or descend floor; *px/*py become spawn
    lcd_gameplay_active = 1u; // line-8 ISR applies camera scroll; title/menu keep 0
    window_ui_show();         // WX/WY + bottom panel on window layer
    if (from_pit) {
        floor_num++; // deeper level without resetting HP
    } else {
        floor_num = 1;           // brand-new run starts at floor 1
        player_hp_max = PLAYER_HP_BASE_MAX; // fresh run baseline stats
        player_level = 1;
        player_damage = 1;
        player_xp = 0;
        player_hp = player_hp_max;
    }
    // Derive a unique seed per floor from the fixed run seed + floor number.
    // floor_num differentiates floors; multiply+XOR scrambles bits so adjacent floors don't look like slight shifts of each other.
    uint16_t floor_seed = (uint16_t)(run_seed * 2053u)
                        ^ (uint16_t)(floor_num * 6364u)
                        ^ 0xACE1u;
    if (!floor_seed) floor_seed = 0xACE1u; // initrand(0) is a foot-gun; force non-zero

    floor_ground_init(floor_seed);    // E1–E5 + blank mix; same seed as level RNG so same named run matches

    num_corpses       = 0;              // fresh floor has no corpse markers
    enemy_anim_toggle = 0;              // sync animation phase on level load
    enemy_anim_reset();               // reset DIV accumulator for glyph flip timing
    wall_tileset_index = TILE_WALL_FIRST; // debug: default wall graphic variant
    wall_palette_index = 0;           // default wall ramp; cycle with A for others
    initrand(floor_seed);             // all rand() until next floor uses this seed
    generate_level();                 // carve map + build nav graph
    spawn_enemies();                  // place enemies away from spawn
    *px = START_X;                    // player always starts map center
    *py = START_Y;
    {
        int16_t cx = (int16_t)*px - GRID_W / 2; // camera top-left tile: center player in viewport when possible
        int16_t cy = (int16_t)*py - GRID_H / 2;
        if (cx < 0) cx = 0; // clamp so viewport stays inside MAP_*
        if (cy < 0) cy = 0;
        if (cx > (int16_t)(MAP_W - GRID_W)) cx = (int16_t)(MAP_W - GRID_W);
        if (cy > (int16_t)(MAP_H - GRID_H)) cy = (int16_t)(MAP_H - GRID_H);
        camera_px = (uint16_t)(uint8_t)cx * 8u; // store as pixel scroll (8 px per tile)
        camera_py = (uint16_t)(uint8_t)cy * 8u;
    }
    wait_vbl_done();     // safe before bulk tilemap writes with LCD on
    draw_screen(*px, *py); // full paint: HUD, dungeon ring, bottom UI, scroll regs
}

static void start_new_run(uint8_t *px, uint8_t *py, uint8_t *prev_j,
                           uint16_t *run_entropy) { // title → seed → floor 1
    *run_entropy += 1u + (uint16_t)DIV_REG; // mutate entropy each run so START timing matters
    music_play_title(); // short Am menu loop until START / seed picker finishes
    uint16_t seed = title_screen(*run_entropy); // user may pick word seed or random START
    if (!seed) seed = (uint16_t)(*run_entropy ^ 0xACE1u); // fallback if title returns 0
    if (!seed) seed = 0xACE1u;

    run_seed  = seed;   // fixed for whole run; floors mix from this + floor_num
    floor_num = 0;      // enter_level will set 1 for new run (see from_pit branch)
    *prev_j   = 0;      // no stale edge triggers on first frame after title
    music_play_game();  // long fugue-style loop for dungeon / pit descent
    enter_level(px, py, 0); // from_pit=0 → reset HP and floor counter inside
}

int main(void) {
    DISPLAY_OFF;              // avoid garbage during VRAM setup
    set_default_palette();    // GBDK: DMG-style default before CGB palettes
    load_palettes();          // program all 8 background palette slots
    font_init();
    font_load(font_ibm);      // built-in 8x8 font for '@' and UI glyphs
    font_color(3, 0);         // pen 3, paper 0 in font tile generator
    set_bkg_data(TILESET_VRAM_OFFSET, TILESET_NTILES_VRAM, tileset_tiles); // first 128 sheet tiles → VRAM [128..255]; no font clobber
    SCX_REG = 0;
    SCY_REG = 0;
    SHOW_BKG;
    DISPLAY_ON;
    lcd_init_raster();        // VBL: HUD scroll lock; LYC=8: game camera — before other VBL handlers
    entity_sprites_init();    // OAM palette uses same indices as load_palettes sprite half
    music_init();             // APU on + VBL-driven two-channel loop
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    enable_interrupts();      // wait_vbl_done needs VBlank + LCD STAT for raster

    uint16_t power_on_ticks = (uint16_t)DIV_REG | ((uint16_t)DIV_REG << 8); // widen 8-bit DIV to 16 bits (timing jitter)
    static uint16_t run_entropy; // persists across game_over → new run within same power cycle
    run_entropy = power_on_ticks;
    uint8_t px, py, prev_j = 0; // player tile; prev_j stores last frame's joypad for edge detect
    start_new_run(&px, &py, &prev_j, &run_entropy);

    while (1) {
        uint8_t j  = joypad();       // current held buttons
        uint8_t nx = px, ny = py;   // proposed move target (default: stay)

        if (j & J_LEFT)  nx = px > 0         ? (uint8_t)(px-1) : px; // clamp at map border
        if (j & J_RIGHT) nx = px < MAP_W-1   ? (uint8_t)(px+1) : px;
        if (j & J_UP)    ny = py > 0         ? (uint8_t)(py-1) : py;
        if (j & J_DOWN)  ny = py < MAP_H-1   ? (uint8_t)(py+1) : py;

        if ((j & J_SELECT) && !(prev_j & J_SELECT)) { // edge: cycle wall variants A–G for art debug
            wall_tileset_index = (uint8_t)(wall_tileset_index + 16u); // next row in column A of sheet
            if (wall_tileset_index > TILE_WALL_LAST) wall_tileset_index = TILE_WALL_FIRST;
            wait_vbl_done();
            draw_screen(px, py);
        }
        if ((j & J_A) && !(prev_j & J_A)) { // edge: cycle wall_palette_table
            wall_palette_index = (uint8_t)((wall_palette_index + 1u) % NUM_WALL_PALETTES);
            wait_vbl_done();
            draw_screen(px, py);
        }

        if (nx != px || ny != py) { // attempted move: combat, pit, or walk
            uint8_t ei = enemy_at(nx, ny); // enemy slot at destination, or ENEMY_DEAD
            uint8_t result = 0;            // move_enemies return: 0 ok, 1 hit, 2 player dead

            if (ei != ENEMY_DEAD) { // bump-to-attack: player does not change tile
                {
                    int8_t adx = (nx > px) ? 1 : (nx < px ? -1 : 0);
                    int8_t ady = (ny > py) ? 1 : (ny < py ? -1 : 0);
                    entity_sprites_run_player_lunge(px, py, adx, ady);
                }
                if (enemy_hp[ei] > player_damage) {
                    enemy_hp[ei] = (uint8_t)(enemy_hp[ei] - player_damage); // wound enemy by scaled player damage
                } else {
                    uint8_t kill_xp = enemy_defs[enemy_type[ei]].damage;
                    if (num_corpses < MAX_CORPSES) {
                        corpse_x[num_corpses] = enemy_x[ei];
                        corpse_y[num_corpses] = enemy_y[ei];
                        corpse_tile[num_corpses] = corpse_deco_random(); // L1–L5 random deco
                        num_corpses++;
                    }
                    enemy_x[ei] = ENEMY_DEAD; // remove from AI and rendering
                    grant_xp_from_kill(kill_xp);
                }

                result = move_enemies(px, py); // enemies act after player's attack
                if (enemy_attack_slot != ENEMY_DEAD && (result == 1 || result == 2))
                    entity_sprites_run_enemy_lunge(px, py, enemy_attack_slot, px, py);
                if (result == 1 || result == 2) screen_shake(); // feedback on damage or death
                wait_vbl_done();
                draw_screen(px, py);
                if (result == 2) {
                    game_over_screen();
                    start_new_run(&px, &py, &prev_j, &run_entropy);
                    continue;
                }
#if TURN_DELAY_MS > 0
                delay(TURN_DELAY_MS); // optional pacing between turns
#endif
            } else {
                uint8_t t = tile_at(nx, ny);
                if (t == TILE_WALL) {
                    // solid: no movement, no enemy phase
                } else if (t == TILE_PIT) {
                    if (player_hp < player_hp_max) player_hp++; // heal 1 on successful move
                    wait_vbl_done();
                    draw_cell(px, py, px, py); // clear old '@' before teleport regen
                    enter_level(&px, &py, 1);   // new floor, keep HP, increment floor_num
                } else {
                    uint8_t opx = px, opy = py;
                    wait_vbl_done();
                    draw_cell(px, py, px, py); // BG under old tile only; sprite still at old until scroll
                    px = nx; // commit walk
                    py = ny;
                    if (player_hp < player_hp_max) player_hp++; // heal 1 on successful move
                    {
                        uint8_t target_cx = (px > GRID_W / 2) ? (uint8_t)(px - GRID_W / 2) : 0; // camera tile that keeps player centered
                        uint8_t target_cy = (py > GRID_H / 2) ? (uint8_t)(py - GRID_H / 2) : 0;
                        if (target_cx > (uint8_t)(MAP_W - GRID_W)) target_cx = (uint8_t)(MAP_W - GRID_W); // clamp camera
                        if (target_cy > (uint8_t)(MAP_H - GRID_H)) target_cy = (uint8_t)(MAP_H - GRID_H);
                        scroll_toward(target_cx, target_cy, opx, opy, px, py);
                    }
                    result = move_enemies(px, py); // enemy turn after player moved
                    if (enemy_attack_slot != ENEMY_DEAD && (result == 1 || result == 2))
                        entity_sprites_run_enemy_lunge(px, py, enemy_attack_slot, px, py);
                    if (result == 1 || result == 2) screen_shake();
                    wait_vbl_done();
                    draw_enemy_cells(px, py); // lightweight: terrain under sprites + HUD + OAM (scroll_toward already drew the full viewport)

                    if (result == 2) {
                        game_over_screen();
                        start_new_run(&px, &py, &prev_j, &run_entropy);
                        continue;
                    }
#if TURN_DELAY_MS > 0
                    delay(TURN_DELAY_MS);
#endif
                }
            }
        }

        if (enemy_anim_update()) { // periodic glyph flip for living enemies
            wait_vbl_done();
            draw_enemy_cells(px, py); // redraws ring incl. row 0; draw_enemy_cells ends with HUD repaint
        }
        prev_j = j; // save for next frame edge detection
    }
}
