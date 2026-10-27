// Replay the real Mac lifecycle adapter without AppKit windows or game files.
#include "../front_mac.m"
#include "../front_kbm_mac.m"
#include <stdio.h>
static int focus = 1, mode = 2, audio_paused, gfx_paused, audio_ops, gfx_ops, clock_ops, resumed_too_early;
static unsigned clock_reasons;
void guest_clock_freeze_for(int reason, int on) { if (on) clock_reasons |= 1u << reason; else clock_reasons &= ~(1u << reason); clock_ops++; }
int dm_focus_mode(void) { return mode; }
void dm_focus_policy_observe(void (*changed)(void)) { (void)changed; }
void dm_pad_motion(int p, const dm_motion *m) { (void)p; (void)m; }
int dm_mouse_camera(void) { return 0; }
int dm_mouse_camera_kind(void) { return 0; }
double dm_mouse_camera_idle(void) { return 0; }
void dm_mouse_move(int32_t dx, int32_t dy) { (void)dx; (void)dy; }
int g_swkbd_min_chars;
void dm_input_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { (void)b; (void)lx; (void)ly; (void)rx; (void)ry; }
void dm_pad_set_player(int p, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { (void)p; (void)b; (void)lx; (void)ly; (void)rx; (void)ry; }
void dm_pad_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { dm_pad_set_player(0, b, lx, ly, rx, ry); }
void dm_pad_players(uint32_t mask) { (void)mask; }
uint32_t dm_player_npad(int p) { return (uint32_t)p; }
void dm_touch_set(const dm_touch *t, int n) { (void)t; (void)n; }
void dm_focus_set(int n) { focus = n; if (n == 1 && (clock_reasons & 2u || audio_paused || gfx_paused)) resumed_too_early++; }
void dm_audio_suspend(int n) { audio_paused = n; audio_ops++; }
void dm_gfx_suspend(int n) { gfx_paused = n; gfx_ops++; }
void dm_haptics_apple_init(void) {}
void dm_haptics_apple_controller(int p, GCController *c) { (void)p; (void)c; }

static int checks, fails;
#define CHECK(c, s) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", s); } } while (0)
static void reset(void) {
    mode = 2; front_focus_update(1, 1, 0);
    focus = 1; audio_paused = gfx_paused = audio_ops = gfx_ops = clock_ops = resumed_too_early = 0; clock_reasons = 0;
}
int main(void) { @autoreleasepool {
    reset(); front_focus_update(0, 1, 0);
    CHECK(focus == 1, "fullscreen key-window loss does not report a Switch library applet");
    CHECK(!front_input_on && !clock_reasons && !audio_paused && !gfx_paused, "transient key loss only releases input");
    front_focus_update(1, 1, 0);
    CHECK(front_input_on && focus == 1 && !clock_ops, "key restoration in the same app does not suspend or resume execution");
    reset(); front_focus_update(0, 0, 0);
    CHECK(focus == 3, "inactive app maps to Background, not library-applet OutOfFocus");
    CHECK((clock_reasons & 2u) && audio_paused && gfx_paused, "background pauses guest clock, audio and graphics together");
    int clocks = clock_ops, audios = audio_ops, graphics = gfx_ops;
    front_focus_update(0, 0, 0);
    CHECK(clock_ops == clocks && audio_ops == audios && gfx_ops == graphics, "duplicate window/app notifications do not suspend twice");
    front_focus_update(1, 1, 0);
    CHECK(focus == 1 && !clock_reasons && !audio_paused && !gfx_paused && front_input_on, "foreground restores all background-owned resources");
    CHECK(!resumed_too_early, "focus regained is delivered only after execution resumes");
    reset(); front_focus_update(0, 0, 1); front_focus_update(0, 0, 0);
    CHECK(focus == 3 && (clock_reasons & 2u) && gfx_paused, "restoring an inactive minimized window does not resume the app");
    front_focus_update(1, 1, 0);
    CHECK(focus == 1 && !clock_reasons && !gfx_paused, "activation after deminiaturizing resumes once");
    for (int m = 0; m < 4; m++) {
        reset(); mode = m; front_focus_update(0, 0, 0);
        CHECK(focus == 3, "all focus modes report actual application state");
        CHECK(!!(clock_reasons & 2u) == (m != 1) && !!gfx_paused == (m != 1), "NoSuspend runs in background; other modes suspend");
        front_focus_update(1, 1, 0);
        CHECK(!clock_reasons && !gfx_paused && !audio_paused, "each focus mode restores foreground without a stale suspension");
    }
    reset(); mode = 1; front_focus_update(0, 0, 0); mode = 2; front_focus_update(0, 0, 0);
    CHECK((clock_reasons & 2u) && gfx_paused, "policy updates take effect without another focus transition");
    mode = 1; front_focus_update(0, 0, 0);
    CHECK(!clock_reasons && !gfx_paused && focus == 3, "NoSuspend policy can release background execution without fabricating focus");
    reset(); clock_reasons = 4u; front_focus_update(0, 0, 0); front_focus_update(1, 1, 0);
    CHECK(clock_reasons == 4u, "app foreground does not release the software keyboard's clock freeze");
    printf("Mac lifecycle: %d checks, %d failed\n", checks, fails);
    return fails != 0;
} }
