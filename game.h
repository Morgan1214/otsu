// game.h - public interface for the gameplay loop

#pragma once

// Runs the gameplay using the provided beatmap (.osu) and audio (.ogg) paths.
// Returns 0 on success, non-zero on error.
int game_run(const char* osu_path, const char* ogg_path);

