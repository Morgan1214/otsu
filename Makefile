## Simple build for NotHackCMU demo with SDL2_mixer + notcurses

# Toolchain
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?=

# pkg-config helper (override if needed)
PKG ?= pkg-config

# Detect pkg-config package names with fallbacks
SDL2_PKG ?= $(shell $(PKG) --exists sdl2 && echo sdl2 || echo SDL2)
MIXER_PKG ?= $(shell \
  ($(PKG) --exists SDL2_mixer && echo SDL2_mixer) || \
  ($(PKG) --exists sdl2_mixer && echo sdl2_mixer) )
# notcurses is sometimes exposed as notcurses-core
NOTCURSES_PKG ?= $(shell \
  ($(PKG) --exists notcurses && echo notcurses) || \
  ($(PKG) --exists notcurses-core && echo notcurses-core) )

# Gather flags from pkg-config (users can override/append via environment)
SDL2_CFLAGS ?= $(shell $(PKG) --cflags $(SDL2_PKG) 2>/dev/null)
SDL2_LIBS   ?= $(shell $(PKG) --libs   $(SDL2_PKG) 2>/dev/null)
MIXER_CFLAGS ?= $(shell $(PKG) --cflags $(MIXER_PKG) 2>/dev/null)
MIXER_LIBS   ?= $(shell $(PKG) --libs   $(MIXER_PKG) 2>/dev/null)
NOTCURSES_CFLAGS ?= $(shell $(PKG) --cflags $(NOTCURSES_PKG) 2>/dev/null)
NOTCURSES_LIBS   ?= $(shell $(PKG) --libs   $(NOTCURSES_PKG) 2>/dev/null)

# Final flags
CPPFLAGS += $(SDL2_CFLAGS) $(MIXER_CFLAGS) $(NOTCURSES_CFLAGS)
LDLIBS   += $(SDL2_LIBS)   $(MIXER_LIBS)   $(NOTCURSES_LIBS)

BIN    ?= demo
SOURCES = main.c game.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean run print-deps

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# Convenience: run with variables
# Usage: make run OSU=path/to/beatmap.osu OGG=path/to/music.ogg
run: $(BIN)
	@if [ -z "$(OSU)" ] || [ -z "$(OGG)" ]; then \
	  echo "Usage: make run OSU=path/to/beatmap.osu OGG=path/to/music.ogg"; \
	  exit 2; \
	fi
	./$(BIN) "$(OSU)" "$(OGG)"

print-deps:
	@echo "SDL2_PKG=$(SDL2_PKG)"; \
	echo "MIXER_PKG=$(MIXER_PKG)"; \
	echo "NOTCURSES_PKG=$(NOTCURSES_PKG)"; \
	echo "SDL2_CFLAGS=$(SDL2_CFLAGS)"; \
	echo "SDL2_LIBS=$(SDL2_LIBS)"; \
	echo "MIXER_CFLAGS=$(MIXER_CFLAGS)"; \
	echo "MIXER_LIBS=$(MIXER_LIBS)"; \
	echo "NOTCURSES_CFLAGS=$(NOTCURSES_CFLAGS)"; \
	echo "NOTCURSES_LIBS=$(NOTCURSES_LIBS)"

clean:
	rm -f $(BIN) $(OBJECTS)
