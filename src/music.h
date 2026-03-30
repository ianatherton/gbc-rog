#ifndef GAME_MUSIC_H
#define GAME_MUSIC_H

void music_init(void);
void music_play_title(void); // BWV 873 prelude — CH1 + wave bass
void music_play_game(void);  // BWV 873 fugue
void sfx_lunge_hit(void);   // CH4 noise — player or enemy strike (one-shot)

#endif // GAME_MUSIC_H
