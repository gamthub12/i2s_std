#pragma once

#include <stdbool.h>
#include <stddef.h>

void audio_player_start(void);
void audio_player_play(const char *filepath);
void audio_player_play_loop(const char *filepath);
void audio_player_play_embedded(void);
void audio_player_set_volume(unsigned percent);
void audio_player_stop(void);
void audio_player_get_status(bool *playing, char *filepath, size_t filepath_size);
