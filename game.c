// game.c - gameplay loop and rendering/audio integration
#define _GNU_SOURCE
#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// SDL2 audio (OGG via SDL2_mixer if available)
#if defined(__has_include)
#  if __has_include(<SDL2/SDL_mixer.h>)
#    include <SDL2/SDL.h>
#    include <SDL2/SDL_mixer.h>
#    define HAVE_SDL2_MIXER 1
#  elif __has_include(<SDL_mixer.h>) && __has_include(<SDL.h>)
#    include <SDL.h>
#    include <SDL_mixer.h>
#    define HAVE_SDL2_MIXER 1
#  elif __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#    define HAVE_SDL2_CORE 1
#  elif __has_include(<SDL.h>)
#    include <SDL.h>
#    define HAVE_SDL2_CORE 1
#  endif
#endif

#ifdef HAVE_SDL2_MIXER
static Mix_Music* g_music = NULL;
#endif

#include "game.h"

#define INTERVAL_NS 25000000 // 40 FPS
#define NUM_COLS 4
#define BAR_HEIGHT 40

typedef struct {
  uint32_t id;
  struct ncplane* plane;
  int x, y;   // top-left in cells
  int vx, vy; // cells per second
  int w, h;     // size in cells
} Entity;

typedef struct {
  Entity* data;
  size_t len, cap;
  uint32_t next_id;
} EntityVec;

typedef struct {
  int time, type, hitSound, endTime, id;
} hitObject;

typedef struct {
    int notecount, leading, trailing;
    struct ncplane *physical_track;
    uint64_t trackchan;
    hitObject notes[2000];
} track_t;

static track_t track[NUM_COLS];
static EntityVec ents;
static int track_time;
static int S = 0, A = 0, B = 0, F = 0;
static double max_score;

static inline struct timespec ns(long nsec) {
  return (struct timespec){.tv_sec = nsec / 1000000000L, .tv_nsec = nsec % 1000000000L};
}

static void ev_init(EntityVec* v) {
  v->data = NULL;
  v->len = 0;
  v->cap = 0;
  v->next_id = 1;
}

static void ev_reserve(EntityVec* v, size_t need) {
  if (need <= v->cap) return;
  size_t ncap = v->cap ? v->cap * 2 : 8;
  if (ncap < need) ncap = need;
  v->data = (Entity*)realloc(v->data, ncap * sizeof(Entity));
  v->cap = ncap;
}

static Entity* ev_push(EntityVec* v) {
  ev_reserve(v, v->len + 1);
  return &v->data[v->len++];
}

// O(1) remove by swapping with last element; returns true if removed
static bool ev_remove_swap(EntityVec* v, size_t idx) {
  if (idx >= v->len) return false;
  v->data[idx] = v->data[v->len - 1];
  v->len--;
  return true;
}

// Create a rectangle
static struct ncplane* make_rectangle(struct ncplane* parent, int y, int x, int h, int w, uint32_t rgb) {
  struct ncplane_options nopts = {
      .y = y, .x = x, .rows = h, .cols = w, .name = "ent", .flags = 0};
  struct ncplane* p = ncplane_create(parent, &nopts);
  if (!p) return NULL;
  uint64_t chan = 0;
  ncchannels_set_fg_rgb8(&chan, 255, 255, 255);
  ncchannels_set_bg_rgb8(&chan, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  ncplane_set_base(p, " ", 0, chan);
  return p;
}

// Create a beat
static struct ncplane* make_beat(struct ncplane* parent, int y, int x, int h, int w, uint32_t rgb) {
  struct ncplane_options nopts = {
      .y = y, .x = x, .rows = h, .cols = w, .name = "ent", .flags = 0};
  struct ncplane* p = ncplane_create(parent, &nopts);
  if (!p) return NULL;
  uint64_t chan = 0;
  ncchannels_set_fg_rgb8(&chan, 255, 255, 255);
  ncchannels_set_bg_rgb8(&chan, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  ncplane_set_base(p, " ", 0, chan);

  // ---- Border styles ----
  const char *ul = "╭", *ur = "╮", *ll = "╰", *lr = "╯", *hz = "─", *vt = "│";
  uint64_t bchan = 0;
  ncchannels_set_fg_rgb8(&bchan, 200, 255, 255);
  ncchannels_set_bg_rgb8(&bchan, 0, 0, 0);

  // Top & bottom
  ncplane_set_channels(p, bchan);
  ncplane_putegc_yx(p, 0, 0, ul, NULL);
  ncplane_putegc_yx(p, 0, w - 1, ur, NULL);
  ncplane_putegc_yx(p, h - 1, 0, ll, NULL);
  ncplane_putegc_yx(p, h - 1, w - 1, lr, NULL);
  for (int x2 = 1; x2 < w - 1; x2++) {
    ncplane_putegc_yx(p, 0, x2, hz, NULL);
    ncplane_putegc_yx(p, h - 1, x2, hz, NULL);
  }
  for (int y2 = 1; y2 < h - 1; y2++) {
    ncplane_putegc_yx(p, y2, 0, vt, NULL);
    ncplane_putegc_yx(p, y2, w - 1, vt, NULL);
  }
  return p;
}

static struct ncplane* make_progbar(struct ncplane* parent, int y, int x, int h, int w) {
  struct ncplane_options nopts = {
      .y = y, .x = x, .rows = h, .cols = w, .name = "ent", .flags = 0};
  struct ncplane* p = ncplane_create(parent, &nopts);
  if (!p) return NULL;
  uint64_t chan = 0;
  ncchannels_set_fg_rgb8(&chan, 255, 255, 255);
  ncchannels_set_bg_rgb8(&chan, 0, 0, 0);
  ncplane_set_base(p, " ", 0, chan);

  // ---- Border styles ----
  const char *ul = "╭", *ur = "╮", *ll = "╰", *lr = "╯", *hz = "─", *vt = "│";
  uint64_t bchan = 0;
  ncchannels_set_fg_rgb8(&bchan, 255, 255, 255);
  ncchannels_set_bg_rgb8(&bchan, 0, 0, 0);

  // Top & bottom
  ncplane_set_channels(p, bchan);
  ncplane_putegc_yx(p, 0, 0, ul, NULL);
  ncplane_putegc_yx(p, 0, w - 1, ur, NULL);
  ncplane_putegc_yx(p, h - 1, 0, ll, NULL);
  ncplane_putegc_yx(p, h - 1, w - 1, lr, NULL);
  for (int x2 = 1; x2 < w - 1; x2++) {
    ncplane_putegc_yx(p, 0, x2, hz, NULL);
    ncplane_putegc_yx(p, h - 1, x2, hz, NULL);
  }
  for (int y2 = 1; y2 < h - 1; y2++) {
    ncplane_putegc_yx(p, y2, 0, vt, NULL);
    ncplane_putegc_yx(p, y2, w - 1, vt, NULL);
  }
  return p;
}

// ------- Big ASCII font for digits and symbols -------
// 7 rows high, variable width (mostly 5). Glyph maps use '#'
// as a mask; we render visible cells using the full block '█'.
static const char* GLYPH_0[7] = {
  " ### ",
  "#   #",
  "#  ##",
  "# # #",
  "##  #",
  "#   #",
  " ### ",
};
static const char* GLYPH_1[7] = {
  "  #  ",
  " ##  ",
  "  #  ",
  "  #  ",
  "  #  ",
  "  #  ",
  " ### ",
};
static const char* GLYPH_2[7] = {
  " ### ",
  "#   #",
  "    #",
  "   # ",
  "  #  ",
  " #   ",
  "#####",
};
static const char* GLYPH_3[7] = {
  " ### ",
  "#   #",
  "    #",
  "  ## ",
  "    #",
  "#   #",
  " ### ",
};
static const char* GLYPH_4[7] = {
  "   # ",
  "  ## ",
  " # # ",
  "#  # ",
  "#####",
  "   # ",
  "   # ",
};
static const char* GLYPH_5[7] = {
  "#####",
  "#    ",
  "#    ",
  "#### ",
  "    #",
  "#   #",
  " ### ",
};
static const char* GLYPH_6[7] = {
  " ### ",
  "#   #",
  "#    ",
  "#### ",
  "#   #",
  "#   #",
  " ### ",
};
static const char* GLYPH_7[7] = {
  "#####",
  "    #",
  "   # ",
  "  #  ",
  "  #  ",
  "  #  ",
  "  #  ",
};
static const char* GLYPH_8[7] = {
  " ### ",
  "#   #",
  "#   #",
  " ### ",
  "#   #",
  "#   #",
  " ### ",
};
static const char* GLYPH_9[7] = {
  " ### ",
  "#   #",
  "#   #",
  " ####",
  "    #",
  "#   #",
  " ### ",
};
static const char* GLYPH_PCT[7] = {
  "#   #",
  "#  # ",
  "   # ",
  "  #  ",
  " #   ",
  "#  # ",
  "#   #",
};
static const char* GLYPH_SPACE[7] = {
  "     ",
  "     ",
  "     ",
  "     ",
  "     ",
  "     ",
  "     ",
};

typedef struct { const char** rows; int w; } Glyph;

static Glyph glyph_for(char c) {
  switch (c) {
    case '0': return (Glyph){GLYPH_0, 5};
    case '1': return (Glyph){GLYPH_1, 5};
    case '2': return (Glyph){GLYPH_2, 5};
    case '3': return (Glyph){GLYPH_3, 5};
    case '4': return (Glyph){GLYPH_4, 5};
    case '5': return (Glyph){GLYPH_5, 5};
    case '6': return (Glyph){GLYPH_6, 5};
    case '7': return (Glyph){GLYPH_7, 5};
    case '8': return (Glyph){GLYPH_8, 5};
    case '9': return (Glyph){GLYPH_9, 5};
    case '%': return (Glyph){GLYPH_PCT, 5};
    default:  return (Glyph){GLYPH_SPACE, 5};
  }
}

static int text_pixel_width(const char* s) {
  int w = 0;
  for (const char* p = s; *p; ++p) {
    w += glyph_for(*p).w;
    if (*(p+1)) w += 1; // inter-glyph space
  }
  return w;
}

static void draw_big_text(struct ncplane* p, const char* s) {
  if (!p || !s) return;
  // Clear the plane
  ncplane_erase(p);
  // Colors
  uint64_t chan = 0;
  ncchannels_set_fg_rgb8(&chan, 255, 255, 255);
  ncchannels_set_bg_rgb8(&chan, 0, 0, 0);
  ncplane_set_channels(p, chan);

  int pw = text_pixel_width(s);
  int rows = ncplane_dim_y(p);
  int cols = ncplane_dim_x(p);
  int startx = (cols - pw) / 2; if (startx < 0) startx = 0;
  int starty = (rows - 7) / 2; if (starty < 0) starty = 0;

  for (int row = 0; row < 7; ++row) {
    int x = startx;
    for (const char* c = s; *c; ++c) {
      Glyph g = glyph_for(*c);
      const char* line = g.rows[row];
      for (int i = 0; i < g.w && x + i < cols; ++i) {
        char ch = line[i];
        if (ch != ' ') {
          ncplane_putegc_yx(p, starty + row, x + i, "█", NULL);
        }
      }
      x += g.w + 1; // space between glyphs
    }
  }
}

// Spawns an entity; returns its id (or 0 on failure)
static uint32_t spawn_entity(EntityVec* ev, struct ncplane* std,
                             int rows, int cols, int x, int y,
                             int w, int h, float vx, float vy, uint32_t color_rgb) {
  // Clamp size
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > cols) w = cols;
  if (h > rows) h = rows;
  struct ncplane* p = make_beat(std, y, x, h, w, color_rgb);
  if (!p) return 0;

  Entity* e = ev_push(ev);
  e->id = ev->next_id++;
  e->plane = p;
  e->x = (float)x;
  e->y = (float)y;
  e->vx = vx;
  e->vy = vy;
  e->w = w;
  e->h = h;
  return e->id;
}

static void destroy_entity(Entity* e) {
  if (e->plane) {
    ncplane_destroy(e->plane);
    e->plane = NULL;
  }
}

// Removes first entity with given id; returns true if removed
static bool remove_entity_by_id(EntityVec* ev, uint32_t id) {
  for (size_t i = 0; i < ev->len; i++) {
    if (ev->data[i].id == id) {
      destroy_entity(&ev->data[i]);
      ev_remove_swap(ev, i);
      return true;
    }
  }
  return false;
}

static int parse(const char *path) {
    FILE *pFile;
    if ((pFile = fopen(path, "r")) == NULL) {
        return -1;
    }

    int x, y, time, type, hitSound, endTime, i[NUM_COLS] = {0, 0, 0, 0};
    char discard[100];
    for (int j = 0; j < NUM_COLS; j++) {
        track[j].leading = 0;
        track[j].trailing = 0;
        track[j].notecount = 0;
        track[j].trackchan = 0;
    }
    while (!feof(pFile)) {
        fscanf(pFile, "%d,%d,%d,%d,%d,%d%s",
                  &x, &y, &time, &type,
                  &hitSound, &endTime, discard);
        int tnum = x / 128;
        track[tnum].notes[i[tnum]].time = time;
        track[tnum].notes[i[tnum]].type = type;
        track[tnum].notes[i[tnum]].hitSound = hitSound;
        track[tnum].notes[i[tnum]].endTime = endTime;
        track[tnum].notecount++;
        i[tnum]++;
        max_score += 10;
    }
    fclose(pFile);
    return 0;
}

static int judge(track_t *trac) {
    int trail = trac->trailing;
    // No judgement
    if (trail >= trac->notecount ||
        trac->notes[trail].time > track_time + 400) {
        ncchannels_set_bg_rgb8(&trac->trackchan, 100, 100, 100);
        return 0;
    }

    // Bad/miss
    else if (abs(trac->notes[trail].time - track_time) > 200) {
        ncchannels_set_bg_rgb8(&trac->trackchan, 120, 30, 30);
        remove_entity_by_id(&ents, trac->notes[trail].id);
        trac->trailing++;
        F++;
        return 0;
    }

    // Good
    else if (abs(trac->notes[trail].time - track_time) > 120) {
        ncchannels_set_bg_rgb8(&trac->trackchan, 50, 100, 50);
        remove_entity_by_id(&ents, trac->notes[trail].id);
        trac->trailing++;
        B++;
        return 6;
    }

    // Great
    else if (abs(trac->notes[trail].time - track_time) > 60) {
        ncchannels_set_bg_rgb8(&trac->trackchan, 50, 50, 100);
        remove_entity_by_id(&ents, trac->notes[trail].id);
        trac->trailing++;
        A++;
        return 8;
    }

    // Excellent
    else{
        ncchannels_set_bg_rgb8(&trac->trackchan, 100, 100, 50);
        remove_entity_by_id(&ents, trac->notes[trail].id);
        trac->trailing++;
        S++;
        return 10;
    }
}

int game_run(const char* osu_path, const char* ogg_path) {
  max_score = 0;
  if (parse(osu_path)) {
    puts("Error parsing file");
    return 1;
  }

  int score = 0;
  double percentage = 0;
  double elapsed = 0;
  track_time = -5000;
  S = 0; A = 0; B = 0; F = 0;

  struct notcurses* nc = notcurses_init(NULL, NULL);
  if (!nc) {
    fprintf(stderr, "notcurses init failed\n");
    return 1;
  }
  unsigned rows, cols;
  struct ncplane* std = notcurses_stddim_yx(nc, &rows, &cols);
  make_rectangle(std, 0, 0, rows, cols, 0);
  for (int i = 0; i < NUM_COLS; i++) {
    track[i].physical_track = make_rectangle(std, 0, i * 30 + 30, rows, 30, 0);
  }
  ev_init(&ents);
  struct ncplane *judge_bar = make_beat(std, BAR_HEIGHT, 29, 3, 122, (160 << 16) + (160 << 8) + 160);
  uint64_t bar_chan;
  (void)bar_chan;
  ncplane_set_base(judge_bar, "/", 0, -1);

  make_rectangle(std, 0, 150, 25, 36, (180 << 16) + (255 << 8) + 255);
  make_rectangle(std, 1, 151, 23, 34, (240 << 16) + (110 << 8) + 210);
  make_rectangle(std, 2, 152, 21, 32, 0);
  make_rectangle(std, 25, 150, 1, 36, -1);

  make_rectangle(std, 0, 0, rows, 30, (180 << 16) + (255 << 8) + 255);
  make_rectangle(std, 1, 1, rows - 2, 28, (240 << 16) + (110 << 8) + 210);
  make_rectangle(std, 2, 2, rows - 4, 26, 0);
  struct ncplane *progbar = make_progbar(std, 4, 13, rows - 8, 4);

  // Plane to render big percentage text inside the inner rectangle, with 1-cell margin
  struct ncplane_options score_opts = {
      .y = 3, .x = 153, .rows = 19, .cols = 30, .name = "score", .flags = 0};
  struct ncplane* score_plane = ncplane_create(std, &score_opts);
  if (score_plane) {
    uint64_t base = 0;
    ncchannels_set_fg_rgb8(&base, 255, 255, 255);
    ncchannels_set_bg_rgb8(&base, 0, 0, 0);
    ncplane_set_base(score_plane, " ", 0, base);
  }


  struct timespec now, start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  bool running = true;

  // ---- SDL2 Audio Init & Music Load ----
#ifdef HAVE_SDL2_MIXER
  if (SDL_Init(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
  } else {
    int mix_flags = MIX_INIT_OGG;
    int initted = Mix_Init(mix_flags);
    if ((initted & mix_flags) != mix_flags) {
      fprintf(stderr, "Mix_Init OGG failed: %s\n", Mix_GetError());
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
      g_music = Mix_LoadMUS(ogg_path);
      if (!g_music) {
        fprintf(stderr, "Failed to load music '%s': %s\n", ogg_path, Mix_GetError());
      } else {
        // Defer playback until the main loop starts running
      }
    } else {
      fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
    }
  }
#elif defined(HAVE_SDL2_CORE)
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
  } else {
    fprintf(stderr, "SDL2 initialized, but SDL2_mixer not found; OGG playback disabled.\n");
  }
#else
  fprintf(stderr, "SDL2 headers not found at build time; audio disabled.\n");
#endif

  bool music_started = false;
  while (running) {
    // Start music playback on first frame of the main loop
#ifdef HAVE_SDL2_MIXER
    if (track_time >= 0 && !music_started && g_music) {
      if (Mix_PlayMusic(g_music, 0) == -1) {
        fprintf(stderr, "Failed to play music: %s\n", Mix_GetError());
      }
      music_started = true;
    }
#endif

    // Timing
    clock_gettime(CLOCK_MONOTONIC, &now);
    now.tv_nsec += INTERVAL_NS;
    while (now.tv_nsec >= 1000000000) {
      now.tv_nsec -= 1000000000;
      now.tv_sec++;
    }
    track_time = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 - 5000;

    // Drop new notes from above visible screen
    for (int i = 0; i < NUM_COLS; i++) {
        while (track[i].leading < track[i].notecount && track[i].notes[track[i].leading].time - 5000 <= track_time) {
            track[i].notes[track[i].leading].id = spawn_entity(&ents, std, rows, cols, i * 30 + 31,
            -200 + BAR_HEIGHT, 28, 3, 0, 1, (240 << 16) + (110 << 8) + 210);
            track[i].leading++;
            elapsed += 10;
        }
    }

    // Check for missed notes
    for (int i = 0; i < NUM_COLS; i++) {
        while (track[i].trailing < track[i].notecount && track[i].notes[track[i].trailing].time + 400 <= track_time) {
            remove_entity_by_id(&ents, track[i].notes[track[i].trailing].id);
            ncchannels_set_bg_rgb8(&track[i].trackchan, 120, 30, 30);
            track[i].trailing++;
        }
    }

    // ---- Input (non-blocking) ----
    struct timespec zero = ns(0);
    ncinput ni;
    uint32_t key;
    while ((key = notcurses_get(nc, &zero, &ni)) != 0) {
      if (key == 'q' || key == 'Q') {
        running = false;
      } else if (key == 'd' || key == 'D') {
        score += judge(track);
      } else if (key == 'f' || key == 'F') {
        score += judge(track + 1);
      } else if (key == 'j' || key == 'J') {
        score += judge(track + 2);
      } else if (key == 'k' || key == 'K') {
        score += judge(track + 3);
      }
    }

    // Update percentage display (clamped 0..100), integer percent
    percentage = (double)score / max_score * (double)100;
    if (percentage < 0) percentage = 0; if (percentage > 100) percentage = 100;
    int ipercentage = (int)(percentage + 0.5);
    char buf[8];
    if (ipercentage > 100) ipercentage = 100; if (ipercentage < 0) ipercentage = 0;
    snprintf(buf, sizeof(buf), "%d%%", ipercentage);
    if (score_plane) {
      draw_big_text(score_plane, buf);
    }

    for (int i = 3; i < rows - 7; i++) {
      if ((double)i / (double)(rows - 8) <= elapsed / max_score)
        ncplane_putegc_yx(progbar, rows - 7 - i, 1, "█", NULL);
        ncplane_putegc_yx(progbar, rows - 7 - i, 2, "█", NULL);
    }

    // ---- Update all entities ----
    for (size_t i = 0; i < ents.len; /* i advanced below */) {
      Entity* e = &ents.data[i];

      // Integrate
      e->x += e->vx;
      e->y += e->vy;

      // Move its plane to the new integer location
      ncplane_move_yx(e->plane, (int)(e->y), (int)(e->x));

      i++; // normal increment
    }

    for (int i = 0; i < NUM_COLS; i++) {
        unsigned int r, g, b, rgb;
        ncplane_set_base(track[i].physical_track, " ", 0, track[i].trackchan);
        rgb = ncchannels_bchannel(track[i].trackchan);
        r = (rgb >> 16) & 0xFF;
        g = (rgb >> 8) & 0xFF;
        b = rgb & 0xFF;
        r = r * 7 / 8;
        g = g * 7 / 8;
        b = b * 7 / 8;
        ncchannels_set_bg_rgb8(&track[i].trackchan, r, g, b);
    }

    // ---- Render ----
    notcurses_render(nc);

    // ---- Frame pacing ----
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &now, NULL);
  }

  // Cleanup
  for (size_t i = 0; i < ents.len; i++) destroy_entity(&ents.data[i]);
  free(ents.data);
  notcurses_stop(nc);

  // ---- SDL audio cleanup ----
#ifdef HAVE_SDL2_MIXER
  if (Mix_PlayingMusic()) {
    Mix_HaltMusic();
  }
  if (g_music) {
    Mix_FreeMusic(g_music);
    g_music = NULL;
  }
  Mix_CloseAudio();
  Mix_Quit();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
#elif defined(HAVE_SDL2_CORE)
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
  return 0;
}
