// main.c - thin entrypoint that forwards to the game loop
#include <stdio.h>
#include "game.h"

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("Usage: %s <path/to/.osu file> <path/to/.ogg file>\n", argv[0]);
    return 0;
  }
  return game_run(argv[1], argv[2]);
}

