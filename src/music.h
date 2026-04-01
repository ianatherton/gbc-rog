#ifndef GAME_MUSIC_H
#define GAME_MUSIC_H

void music_init(void);
void music_play_title(void); // BWV 1043 — title loops first section (CH1 + wave)
void music_play_game(void);  // BWV 1043 — continues from split through loop
void music_play_levelup_jingle(void); // short CH1 fanfare; resumes BGM after
void music_loading_screen_set(uint8_t on); // 1: mute BGM + six quieting footfalls; 0: resume BGM
void sfx_lunge_hit(void);   // CH4 noise — player or enemy strike (one-shot)

#endif // GAME_MUSIC_H
