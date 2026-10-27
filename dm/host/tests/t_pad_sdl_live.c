// Optional integration check against a real SDL2 library (`brew install sdl2`), with a virtual controller, not hardware.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../pad_sdl_mac.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int32_t axes[4];
static uint64_t buttons;
static uint32_t players;
static void state(void *ctx, int p, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    (void)ctx; assert(p == 0); axes[0] = lx; axes[1] = ly; axes[2] = rx; axes[3] = ry; buttons = b;
}
static void connected(void *ctx, uint32_t mask) { (void)ctx; players = mask; }
int main(void) {
    SDL_SetMainReady();
    char error[512];
    dm_sdl_pad *pad = dm_sdl_pad_create((dm_sdl_pad_sink){.state = state, .players = connected}, 0, 0, getenv("DM_SDL_LIBRARY"), error, sizeof error);
    if (!pad) { fprintf(stderr, "%s\n", error); return 1; }
    // If a physical pad is connected it would own player 1; keep this check deterministic and do not mistake it for virtual input.
    if (SDL_NumJoysticks()) { puts("Disconnect physical controllers before the virtual SDL test."); dm_sdl_pad_destroy(pad); return 2; }
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0 && SDL_IsGameController(index));
    SDL_Joystick *joy = SDL_JoystickOpen(index); assert(joy);
    assert(!SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 24576));
    assert(!SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTY, -16384));
    assert(!SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_RIGHTX, -32768));
    assert(!SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_RIGHTY, 32767));
    assert(!SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, 1));
    dm_sdl_pad_poll(pad, 1);
    printf("Real SDL2 virtual input: LX=%d LY=%d RX=%d RY=%d buttons=%#llx players=%#x\n", axes[0], axes[1], axes[2], axes[3], (unsigned long long)buttons, players);
    assert(players == 1 && axes[0] == 24576 && axes[1] == 16384 && axes[2] == -32767 && axes[3] == -32767 && buttons == DM_BTN_A);
    dm_sdl_pad_poll(pad, 0); assert(!axes[0] && !buttons);
    dm_sdl_pad_poll(pad, 1); assert(axes[0] == 24576);
    SDL_JoystickClose(joy); assert(!SDL_JoystickDetachVirtual(index));
    dm_sdl_pad_poll(pad, 1); assert(!players && !axes[0]);
    dm_sdl_pad_destroy(pad);
    puts("Real SDL2: dynamic ABI, input mapping, focus and detach passed");
    return 0;
}
