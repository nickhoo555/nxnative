// Run the actual composition root and focus handlers against the SDL fixture, without a window or game.
#include "../front_mac.m"
#include "../front_kbm_mac.m"
#include <assert.h>
#include <dlfcn.h>

static int apple_init, apple_routes, writes;
static uint32_t players;
static dm_motion motion;
static int motion_on;
void dm_pad_motion(int p, const dm_motion *m) { (void)p; motion_on = m != NULL; if (m) motion = *m; }
static int32_t left_x;
static int mouse_camera_on, camera_kind;
static int32_t moved_x, moved_y;
int dm_mouse_camera(void) { return mouse_camera_on; }
int dm_mouse_camera_kind(void) { return camera_kind; }
double dm_mouse_camera_idle(void) { return 0; }
void dm_mouse_move(int32_t dx, int32_t dy) { moved_x += dx; moved_y += dy; }
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
    // The optional camera layout cannot activate without a matching game patch.
    kbm_set(1); assert(!kbm_on());
    mouse_camera_on = 1; kbm_set(1); assert(kbm_on());
    kbm_key(49, 1); assert(front_btn & DM_BTN_X);
    kbm_key(49, 0); assert(!(front_btn & DM_BTN_X));
    kbm_key(8, 1); assert(front_btn & DM_BTN_LS);
    kbm_key(8, 0); assert(!(front_btn & DM_BTN_LS));
    // Drive the real motion accumulator without capturing the desktop cursor.
    g_mouse_cap = 1;
    mouse_moved(0.4f, -0.4f); assert(!moved_x && !moved_y);
    mouse_moved(0.7f, -0.7f); assert(moved_x == 1 && moved_y == -1);
    kbm_key(12, 1); mouse_moved(100, 0);
    uint64_t kb = 0; int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    kbm_input(&kb, &lx, &ly, &rx, &ry);
    assert(rx == 32767 && ry == 0 && moved_x == 1);
    kbm_key(12, 0);
    mouse_button(0, 1); assert(g_mbtn & DM_BTN_Y);
    camera_kind = 3; mouse_button(0, 0); assert(!g_mbtn);
    mouse_button(0, 1); assert(g_mbtn & DM_BTN_A);
    mouse_button(0, 0); assert(!g_mbtn);
    g_mouse_cap = 0; kbm_set(0); mouse_camera_on = 0; camera_kind = 0;
    assert(!kbm_on() && !front_btn);
    puts("Keyboard/mouse gate, keys, relative motion, ability wheel and contextual clicks passed");
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
    void (*set_motion)(int, const float *, const float *) = dlsym(lib, "fake_motion"); assert(set_motion);
    unsigned (*rumble)(int) = dlsym(lib, "fake_rumble"); assert(rumble);
    add(0, "S820"); set_motion(0, (float[]){0, 9.80665f, 0}, (float[]){0, 1, 0}); axis(0, 0, 24000); sdl_tick();
    assert(players == 1 && left_x == 24000 && dm_haptics_available() == 1);
    dm_vibration_update u = {.player = 0, .motor = 0, .value = {.amplitude_low = .5f, .frequency_low = 160, .frequency_high = 320}};
    dm_haptics_send(&u, 1); sdl_tick(); assert(rumble(0));
    assert(motion_on && motion.gyro[2] > 0);
    input_release(); assert(!left_x && !rumble(0) && !motion_on);
    int before = writes;
    axis(0, 0, -24000); sdl_tick(); assert(writes == before && !left_x);
    input_resume(); assert(left_x == -24000 && !rumble(0) && motion_on); // input resumes; stale vibration does not
    // No Apple observers were installed: even an Apple connection notification cannot take over SDL's input/haptics route.
    [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidConnectNotification object:nil];
    assert(!apple_init && !apple_routes);
    sdl_shutdown(); assert(!g_sdl_pad && !g_sdl_timer && !players && !left_x && !dm_haptics_available() && !motion_on);
    dlclose(lib);
    puts("Mac composition root: SDL-only ownership, input delivery, focus and haptics passed"); return 0;
} }
