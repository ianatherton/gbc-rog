#ifndef GAME_MUSIC_H
#define GAME_MUSIC_H

void music_init(void);
void music_play_title(void); // BWV 1043 — title loops first section (CH1 + wave)
void music_play_game(void);  // BWV 1043 — continues from split through loop
void music_play_levelup_jingle(void); // short CH1 fanfare; resumes BGM after
void music_loading_screen_set(uint8_t on); // 1: mute BGM + six quieting footfalls; 0: resume BGM
void sfx_lunge_hit(void);   // CH4 noise — player or enemy strike (one-shot)
void sfx_spell_zap(void);   // CH2 short zap for witch bolt cast
void sfx_whirlwind_cast(void); // CH4 burst for zerker Whirlwind cast
void sfx_shield_sparkle(void); // CH2 sparkle for knight shield cast

#endif // GAME_MUSIC_H
