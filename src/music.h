#ifndef GAME_MUSIC_H
#define GAME_MUSIC_H

void music_init(void);
void music_play_title(void); // title loops first section (CH1 + wave)
void music_play_game(void);  // dungeon — continues from split through loop
void music_set_bgm_track(uint8_t track); // 0 = BWV 1043 (default), 1 = BWV 527 organ trio
uint8_t music_get_bgm_track(void);
void music_begin_floor_bgm(void);        // random 1043 vs 527 for current floor, then music_play_game
void music_play_levelup_jingle(void); // short CH1 fanfare; resumes BGM after
void music_loading_screen_set(uint8_t on); // 1: mute BGM + six quieting footfalls; 0: resume BGM
void sfx_lunge_hit(void);   // CH4 noise — player or enemy strike (one-shot)
void sfx_spell_zap(void);   // CH2 short zap for witch bolt cast
void sfx_whirlwind_cast(void); // CH4 burst for zerker Whirlwind cast
void sfx_shield_sparkle(void); // CH2 sparkle for knight shield cast

#endif // GAME_MUSIC_H
