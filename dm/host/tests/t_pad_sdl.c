// Exercise the real dylib loader, SDL polling, player routes and focus handling without a window, SDL installation or game files.
#include "../pad_sdl_mac.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static struct { uint64_t buttons; int32_t lx, ly, rx, ry; int writes; } state[DM_PLAYERS];
static uint32_t players, rumble_routes;
static void receive(void *c, int p, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    (void)c; state[p].buttons = b; state[p].lx = lx; state[p].ly = ly; state[p].rx = rx; state[p].ry = ry; state[p].writes++;
}
static void connected(void *c, uint32_t mask) { (void)c; players = mask; }
static void route(void *c, int p, int on) { (void)c; if (on) rumble_routes |= 1u << p; else rumble_routes &= ~(1u << p); }
static dm_sdl_pad_sink sink = {NULL, receive, connected, route};

int main(int argc, char **argv) {
    assert(argc == 3);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); assert(lib);
    #define F(ret, name, args) ret (*name) args = dlsym(lib, #name); assert(name)
    F(void, fake_reset, (void)); F(void, fake_add, (int, const char *)); F(void, fake_remove, (int));
    F(void, fake_axis, (int, int, int16_t)); F(void, fake_button, (int, int, uint8_t));
    F(int, fake_count, (int)); F(int, fake_hints, (void)); F(void, fake_fail_init, (void)); F(unsigned, fake_rumble, (int));
    #undef F
    char error[512];
    fake_reset();
    assert(!dm_sdl_pad_create(sink, 0, 0, "/no/such/SDL2.dylib", error, sizeof error));
    assert(error[0] && !players && !rumble_routes);
    assert(!dm_sdl_pad_create(sink, 0, 0, argv[2], error, sizeof error));
    assert(strstr(error, "SDL_GameControllerGetAxis"));
    fake_fail_init();
    assert(!dm_sdl_pad_create(sink, 0, 0, argv[1], error, sizeof error));
    assert(strstr(error, "fixture init failure") && !players);
    fake_reset();
    dm_sdl_pad *pad = dm_sdl_pad_create(sink, 0, 0, argv[1], error, sizeof error); assert(pad);
    assert(fake_hints() && fake_count(0) == 1);
    dm_sdl_pad_poll(pad, 1); assert(!players && !state[0].writes);
    // S820 regression: SDL's raw-HID path supplies nonzero left axes even when Apple's extendedGamepad left axes stay zero.
    fake_add(0, "Nintendo Switch Pro Controller");
    fake_axis(0, 0, 24576); fake_axis(0, 1, -16384); fake_axis(0, 2, -32768); fake_axis(0, 3, 32767);
    fake_button(0, 1, 1); // SDL positional B = Nintendo A (right face button)
    dm_sdl_pad_poll(pad, 1);
    assert(players == 1 && rumble_routes == 1);
    assert(state[0].lx == 24576 && state[0].ly == 16384);
    assert(state[0].rx == -32767 && state[0].ry == -32767 && state[0].buttons == DM_BTN_A);
    int writes = state[0].writes;
    dm_sdl_pad_poll(pad, 1); assert(state[0].writes == writes); // do not replace a DM_KEYS/DM_CTL hold with an unchanged physical state
    // Every SDL2 standard button and both trigger axes, with the same positional mapping as GameController.
    static const uint64_t bits[15] = {DM_BTN_B, DM_BTN_A, DM_BTN_Y, DM_BTN_X, DM_BTN_MINUS, 0, DM_BTN_PLUS, DM_BTN_LS, DM_BTN_RS, DM_BTN_L, DM_BTN_R, DM_BTN_UP, DM_BTN_DOWN, DM_BTN_LEFT, DM_BTN_RIGHT};
    for (int b = 0; b < 15; b++) {
        for (int j = 0; j < 15; j++) fake_button(0, j, j == b);
        dm_sdl_pad_poll(pad, 1); assert(state[0].buttons == bits[b]);
    }
    fake_axis(0, 4, 32767); fake_axis(0, 5, 32767); dm_sdl_pad_poll(pad, 1);
    assert((state[0].buttons & (DM_BTN_ZL | DM_BTN_ZR)) == (DM_BTN_ZL | DM_BTN_ZR));
    dm_sdl_pad_rumble(pad, 0, 1234, 5678); assert(fake_rumble(0) == (1234u | 5678u << 16));
    dm_sdl_pad_poll(pad, 0); assert(!state[0].buttons && !state[0].lx && !state[0].ly && !fake_rumble(0));
    writes = state[0].writes;
    fake_axis(0, 0, -20000); dm_sdl_pad_poll(pad, 0); assert(state[0].writes == writes);
    dm_sdl_pad_poll(pad, 1); assert(state[0].lx == -20000); // re-read held input on focus regain
    fake_add(1, "Second controller"); dm_sdl_pad_poll(pad, 1); assert(players == 3);
    fake_remove(0); dm_sdl_pad_poll(pad, 1); assert(players == 2 && !state[0].lx && rumble_routes == 2);
    fake_add(2, "Nintendo Switch Pro Controller"); dm_sdl_pad_poll(pad, 1); assert(players == 3); // reconnect keeps player 1
    for (int i = 3; i < 12; i++) fake_add(i, "Extra controller");
    dm_sdl_pad_poll(pad, 1); assert(players == 255);
    dm_sdl_pad_destroy(pad); assert(!players && !rumble_routes && fake_count(0) == 0 && fake_count(1) == fake_count(2));
    fake_reset(); memset(state, 0, sizeof state);
    pad = dm_sdl_pad_create(sink, 0, 1, argv[1], error, sizeof error); assert(pad);
    fake_add(0, "Pad"); fake_axis(0, 0, 12345); dm_sdl_pad_poll(pad, 1);
    assert(players == 3 && !state[0].writes && state[1].lx == 12345); // keyboard keeps player 1
    dm_sdl_pad_destroy(pad); assert(!state[0].writes);
    fake_reset(); memset(state, 0, sizeof state);
    pad = dm_sdl_pad_create(sink, 1, 0, argv[1], error, sizeof error); assert(pad);
    fake_add(0, "One"); fake_add(1, "Two"); fake_axis(0, 0, 12345); fake_button(1, 0, 1);
    dm_sdl_pad_poll(pad, 1); assert(players == 1 && state[0].lx == 12345 && state[0].buttons == DM_BTN_B);
    fake_remove(1); dm_sdl_pad_poll(pad, 1); assert(players == 1 && state[0].lx == 12345 && !state[0].buttons);
    fake_remove(0); dm_sdl_pad_poll(pad, 1); assert(!players && !state[0].lx);
    dm_sdl_pad_destroy(pad); dlclose(lib);
    puts("SDL pad: loader, axes/buttons, focus, scripts, hotplug, slots, merge and rumble passed");
    return 0;
}
