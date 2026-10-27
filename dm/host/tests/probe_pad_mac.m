// Hardware check of the production SDL plugin, independent of game files. Built by scripts/check_mac_controller.py.
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#include "../pad_sdl_mac.h"
#include <math.h>

static double sdl_peak[4], apple_peak[4];
static uint32_t connected;
static uint64_t buttons;
static double gyro_peak;
static int motion_samples;
static void motion(void *ctx, int p, const dm_motion *m) {
    (void)ctx; (void)p; if (!m) return;
    motion_samples++;
    double rate = sqrt(m->gyro[0]*m->gyro[0] + m->gyro[1]*m->gyro[1] + m->gyro[2]*m->gyro[2]);
    gyro_peak = fmax(gyro_peak, rate);
}
static void sample(void *ctx, int player, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    (void)ctx; int32_t axes[] = {lx, ly, rx, ry}; int changed = (b & ~buttons) != 0; buttons |= b;
    for (int i = 0; i < 4; i++) {
        double magnitude = fabs((double)axes[i]) / DM_STICK_MAX;
        if (magnitude > sdl_peak[i] + .1) changed = 1;
        sdl_peak[i] = fmax(sdl_peak[i], magnitude);
    }
    if (changed) printf("SDL activity: player=%d L=(%d,%d) R=(%d,%d) buttons=%#llx\n", player + 1, lx, ly, rx, ry, (unsigned long long)b);
}
static void players(void *ctx, uint32_t mask) { (void)ctx; connected = mask; printf("SDL connected players=%#x\n", mask); }
int main(int argc, char **argv) { @autoreleasepool {
    setbuf(stdout, NULL);
    GCController.shouldMonitorBackgroundEvents = YES;
    char error[512];
    dm_sdl_pad *pad = dm_sdl_pad_create((dm_sdl_pad_sink){.state = sample, .players = players, .motion = motion}, 0, 0, getenv("DM_SDL_LIBRARY"), error, sizeof error);
    if (!pad) { fprintf(stderr, "[RED] %s\n", error); return 2; }
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    int motion_check = argc > 2 && !strcmp(argv[2], "motion");
    if (motion_check) printf("For %d seconds: rotate the controller in pitch, yaw and roll.\n", seconds);
    else printf("SDL2 ready. For %d seconds: circle the left stick, then the right stick; press the face buttons and D-pad.\n", seconds);
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (end.timeIntervalSinceNow > 0) {
        dm_sdl_pad_poll(pad, 1);
        for (GCController *c in GCController.controllers) {
            GCExtendedGamepad *g = c.extendedGamepad; if (!g) continue;
            float axes[] = {g.leftThumbstick.xAxis.value, g.leftThumbstick.yAxis.value, g.rightThumbstick.xAxis.value, g.rightThumbstick.yAxis.value};
            for (int i = 0; i < 4; i++) apple_peak[i] = fmax(apple_peak[i], fabs(axes[i]));
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.0 / 120]];
    }
    printf("SDL peak:   LX=%.3f LY=%.3f RX=%.3f RY=%.3f buttons=%#llx players=%#x\n", sdl_peak[0], sdl_peak[1], sdl_peak[2], sdl_peak[3], (unsigned long long)buttons, connected);
    printf("Apple peak: LX=%.3f LY=%.3f RX=%.3f RY=%.3f\n", apple_peak[0], apple_peak[1], apple_peak[2], apple_peak[3]);
    printf("SDL motion: samples=%d peak=%.4f revolutions/s\n", motion_samples, gyro_peak);
    uint32_t measured_players = connected; // shutdown publishes a zero player mask; judge the capture, not teardown
    dm_sdl_pad_destroy(pad);
    if (measured_players & (measured_players - 1)) { puts("INCONCLUSIVE: connect only the controller under test, then retry."); return 2; }
    if (motion_check) {
        if (!measured_players || !motion_samples) { puts("INCONCLUSIVE: no controller with readable six-axis sensors."); return 2; }
        if (gyro_peak < .05) { puts("INCONCLUSIVE: no deliberate rotation measured; rotate the controller and retry."); return 2; }
        puts("PASS: real angular velocity reached the production motion backend (not an in-game aiming test)."); return 0;
    }
    double left = fmax(sdl_peak[0], sdl_peak[1]), right = fmax(sdl_peak[2], sdl_peak[3]);
    if (!measured_players || (left < .5 && right < .5)) { puts("INCONCLUSIVE: no controller or no large stick movement; retry while moving both sticks."); return 2; }
    if (left < .5 || right < .5) { puts("FAIL: one stick did not exceed 0.5; retry after circling both sticks."); return 1; }
    puts("PASS: both sticks reached the SDL backend. This is an input check, not an in-game or iPad test.");
    return 0;
} }
