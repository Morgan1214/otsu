// main.c - thin entrypoint that forwards to the game loop
#include <stdio.h>
#include "game.h"

int main(int argc, char* argv[]) {
    char beatmap_path[100], soundtrack_path[100];
  if (argc != 2) {
    printf("Usage: %s <Test directory>\n", argv[0]);
    return 0;
  }
  snprintf(beatmap_path, 100, "%s/beatmap.otsu", argv[1]);
  snprintf(soundtrack_path, 100, "%s/audio.ogg", argv[1]);
  return game_run(beatmap_path, soundtrack_path);
}

