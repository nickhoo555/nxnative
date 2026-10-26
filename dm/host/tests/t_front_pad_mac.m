// Run the actual composition root and focus handlers against the SDL fixture, without a window or game.
#include "../front_mac.m"
#include <assert.h>
#include <dlfcn.h>

static int apple_init, apple_routes, writes;
static uint32_t players;
static int32_t left_x;
int g_swkbd_min_chars;
void dm_input_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { (void)b; (void)lx; (void)ly; (void)rx; (void)ry; }
void dm_pad_set_player(int p, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { (void)p; (void)b; (void)ly; (void)rx; (void)ry; left_x = lx; writes++; }
void dm_pad_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { dm_pad_set_player(0, b, lx, ly, rx, ry); }
void dm_pad_players(uint32_t mask) { players = mask; }
uint32_t dm_player_npad(int p) { return (uint32_t)p; }
void dm_touch_set(const dm_touch *t, int n) { (void)t; (void)n; }
void dm_focus_set(int n) { (void)n; }
void dm_audio_suspend(int n) { (void)n; }
void dm_gfx_suspend(int n) { (void)n; }
void dm_haptics_apple_init(void) { apple_init++; }
void dm_haptics_apple_controller(int p, GCController *c) { (void)p; (void)c; apple_routes++; }

int main(int argc, char **argv) { @autoreleasepool {
    assert(argc == 3);
    setenv("DM_PAD_BACKEND", "sdl", 1); setenv("DM_SDL_LIBRARY", argv[1], 1);
    unsetenv("DM_PAD_MERGE"); unsetenv("DM_KBD_PLAYER"); unsetenv("DM_RUMBLE"); unsetenv("DM_RUMBLE_GAIN");
    int fallback = !strcmp(argv[2], "fallback");
    int apple = !strcmp(argv[2], "apple");
    if (apple) unsetenv("DM_PAD_BACKEND");
    pads_start();
    if (fallback || apple) {
        assert(!g_sdl_pad && !g_sdl_timer && apple_init == 1);
        puts(apple ? "Mac composition root: default remains Apple, no SDL loaded" : "Mac composition root: missing SDL falls back to Apple"); return 0;
    }
    assert(g_sdl_pad && g_sdl_timer && !apple_init && !apple_routes);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); assert(lib);
    void (*add)(int, const char *) = dlsym(lib, "fake_add"); assert(add);
    void (*axis)(int, int, int16_t) = dlsym(lib, "fake_axis"); assert(axis);
    unsigned (*rumble)(int) = dlsym(lib, "fake_rumble"); assert(rumble);
    add(0, "S820"); axis(0, 0, 24000); sdl_tick();
    assert(players == 1 && left_x == 24000 && dm_haptics_available() == 1);
    dm_vibration_update u = {.player = 0, .motor = 0, .value = {.amplitude_low = .5f, .frequency_low = 160, .frequency_high = 320}};
    dm_haptics_send(&u, 1); sdl_tick(); assert(rumble(0));
    input_release(); assert(!left_x && !rumble(0));
    int before = writes;
    axis(0, 0, -24000); sdl_tick(); assert(writes == before && !left_x);
    input_resume(); assert(left_x == -24000 && !rumble(0)); // input resumes; stale vibration does not
    // No Apple observers were installed: even an Apple connection notification cannot take over SDL's input/haptics route.
    [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidConnectNotification object:nil];
    assert(!apple_init && !apple_routes);
    sdl_shutdown(); assert(!g_sdl_pad && !g_sdl_timer && !players && !left_x && !dm_haptics_available());
    dlclose(lib);
    puts("Mac composition root: SDL-only ownership, input delivery, focus and haptics passed"); return 0;
} }
