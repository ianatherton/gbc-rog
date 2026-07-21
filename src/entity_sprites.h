#ifndef ENTITY_SPRITES_H
#define ENTITY_SPRITES_H

#include "defs.h"

void entity_sprites_init(void) BANKED; // palettes + SHOW_SPRITES; hide OAM slots
void entity_sprites_poof_clear_all(void) BANKED; // new floor / reset — zero death-puff timers
void entity_sprites_enemy_poof_begin(uint8_t slot) BANKED; // killed enemy: cloud until TTL (after corpse BG drawn)

/* Glide: sprite wx/wy includes walk bob; aura_wx/aura_y is smooth track without bob (same X as sprite here). */
void entity_sprites_set_player_world(int16_t spr_wx, int16_t spr_wy, int16_t aura_wx, int16_t aura_wy) BANKED;
void entity_sprites_clear_player_world(void) BANKED; // use px*8, py*8 after scroll ends
void entity_sprites_set_player_facing(int8_t dir_x) BANKED; // -1 left (flip), +1 right (normal)
void entity_sprites_player_hurt_flash(void) BANKED; // ~1 s red ↔ gold on OCP PAL_PLAYER (~8-frame beat)
void entity_sprites_level_up_fx_trigger(void) BANKED; // brief L10 smile on SP_PLAYER_AURA over hero (same slot as ground aura)
void entity_sprites_vbl_tick(void) BANKED; // 60Hz timers for palette flashes

void entity_sprites_refresh_player_only(uint8_t px, uint8_t py) BANKED; // update cached player tile + player OAM only
void entity_sprites_refresh_enemy(uint8_t slot) BANKED; // update one enemy OAM slot or hide it
void entity_sprites_refresh_oam_only(uint8_t px, uint8_t py) BANKED; // player + enemies + belt/hide sweep; no pit ladder reprobe
void entity_sprites_refresh_all(uint8_t px, uint8_t py) BANKED; // refresh_oam_only + ladder cache for pit-arrow VBL

#define ENTITY_LUNGE_FRAMES        10u // ~167ms at 60Hz; was 6 (~100ms)
#define ENTITY_LUNGE_HIT_FLASH_NONE 255u // 5th arg to run_player_lunge — skip mid-strike palette pop
void entity_sprites_run_player_lunge(uint8_t px, uint8_t py, int8_t dir_x, int8_t dir_y, uint8_t hit_enemy_slot) BANKED;
void entity_sprites_enemy_hit_flash_clear(uint8_t slot) BANKED; // stop hit ramp if enemy dies same turn
void entity_sprites_run_enemy_lunge(uint8_t px, uint8_t py, uint8_t slot, uint8_t tgx, uint8_t tgy) BANKED;
void entity_sprites_run_item_popout(uint8_t kind) BANKED; // holds the item's icon beside the hero for 500ms
void entity_sprites_run_enemy_lunges_batch(uint8_t px, uint8_t py, const uint8_t *slots, uint8_t count) BANKED; // concurrent lunge for all attackers
void entity_sprites_run_ally_lunge(uint8_t px, uint8_t py, uint8_t ally_slot, uint8_t tgx, uint8_t tgy, uint8_t hit_enemy_slot) BANKED;
void entity_sprites_run_projectile(uint8_t sx, uint8_t sy, uint8_t tx, uint8_t ty, uint8_t tile_off, uint8_t pal) BANKED;

#define SP_INV_CURSOR 38u // OAM slot for the bouncing menu/inventory cursor sprite

void entity_sprites_inv_cursor_show(uint8_t cx, uint8_t cy) BANKED; // bouncing up-arrow below selected inventory cell
void entity_sprites_inv_cursor_hide(void) BANKED;
void entity_sprites_equip_marks_hide(void) BANKED; // hide OAM slots used by draw_equipped_marks; call on inventory exit

void entity_sprites_run_enemy_glide(uint8_t px, uint8_t py,
                                     const uint8_t *old_ex, const uint8_t *old_ey,
                                     const uint8_t *old_alive) BANKED;

// Split glide for interleaving with camera scroll: call begin() before scroll, step() each scroll VBL,
// then finish() after scroll to drain any remaining offset frames and handle enemies that died during AI.
void entity_sprites_enemy_glide_begin(const uint8_t *old_ex, const uint8_t *old_ey,
                                       const uint8_t *old_alive) BANKED;
void entity_sprites_ally_glide_begin(const uint8_t *old_ax, const uint8_t *old_ay,
                                      const uint8_t *old_aa) BANKED;
void entity_sprites_enemy_glide_step(void) BANKED; // steps en_ofs + ally_ofs (+ town villager offsets); no-op when all zero
void entity_sprites_run_enemy_glide_finish(const uint8_t *old_alive) BANKED; // drain remaining offsets after camera scroll

// Town villager glide: called once per villager that took a plain 1-tile wander step this turn
// (biome_town.c town_npcs_tick) — sets the borrowed en_ofs_x/y[idx] slide-from offset; a warp-home
// jump skips this call entirely so it appears as an instant teleport, matching the ally convention.
void entity_sprites_town_npc_glide_set(uint8_t idx, uint8_t old_x, uint8_t old_y) BANKED;

// Barrel break (biome_town.c town_barrel_try_break): same grey puff art/duration as an enemy's death
// poof, blocking for ~370ms (like entity_sprites_run_item_popout) since it's a discrete one-off turn
// action, not concurrent gameplay — no per-VBL-tick bookkeeping needed.
void entity_sprites_run_barrel_poof(uint8_t mx, uint8_t my) BANKED;

#endif // ENTITY_SPRITES_H
