// Optional Mac input plugin. SDL2 is loaded at runtime; the default GameController path and iOS need no SDL headers or library.
// Create, poll, rumble and destroy on the AppKit thread. The sink owns the game's input/haptics routes; SDL owns device decoding and calibration.
#pragma once
#include <stddef.h>
#include "dm_input.h"

typedef struct dm_sdl_pad dm_sdl_pad;
typedef struct {
    void *context;
    void (*state)(void *context, int player, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
    void (*players)(void *context, uint32_t mask);
    void (*rumble_route)(void *context, int player, int available);
} dm_sdl_pad_sink;

// library: explicit dylib path, or NULL for the Homebrew/system search. Failure leaves no SDL subsystem or input route behind.
dm_sdl_pad *dm_sdl_pad_create(dm_sdl_pad_sink sink, int merge, int keyboard_own, const char *library, char *error, size_t capacity);
void dm_sdl_pad_poll(dm_sdl_pad *pad, int active);
void dm_sdl_pad_rumble(dm_sdl_pad *pad, int player, uint16_t low, uint16_t high);
void dm_sdl_pad_destroy(dm_sdl_pad *pad);
