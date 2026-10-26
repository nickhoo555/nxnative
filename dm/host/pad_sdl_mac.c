// SDL2's Switch HIDAPI driver can read third-party pads whose left stick GameController reports as zero.
// Keep SDL optional and Mac-only: just the stable SDL2 C ABI below, no SDL headers, link flags, video initialization or event pump.
// ABI reference: https://github.com/libsdl-org/SDL/tree/SDL2/include (SDL2 >= 2.0.18 for HasRumble).
#include "pad_sdl_mac.h"
#include "pad_slots.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <time.h>

typedef struct _SDL_GameController SDL_GameController;
enum { SDL_INIT_GAMECONTROLLER = 0x2000, SDL_HINT_OVERRIDE = 2 };
enum { AX_LEFTX, AX_LEFTY, AX_RIGHTX, AX_RIGHTY, AX_TRIGGERLEFT, AX_TRIGGERRIGHT };
enum { BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_BACK, BUTTON_GUIDE, BUTTON_START,
       BUTTON_LEFTSTICK, BUTTON_RIGHTSTICK, BUTTON_LEFTSHOULDER, BUTTON_RIGHTSHOULDER,
       BUTTON_DPAD_UP, BUTTON_DPAD_DOWN, BUTTON_DPAD_LEFT, BUTTON_DPAD_RIGHT, BUTTON_COUNT };

// Function pointers are per plugin instance, so a failed load cannot leave a partially usable backend.
#define SDL_API(F) \
    F(int, SetHintWithPriority, (const char *, const char *, int)) \
    F(int, InitSubSystem, (uint32_t)) \
    F(void, QuitSubSystem, (uint32_t)) \
    F(const char *, GetError, (void)) \
    F(int, NumJoysticks, (void)) \
    F(int, IsGameController, (int)) \
    F(int32_t, JoystickGetDeviceInstanceID, (int)) \
    F(SDL_GameController *, GameControllerOpen, (int)) \
    F(void, GameControllerClose, (SDL_GameController *)) \
    F(int, GameControllerGetAttached, (SDL_GameController *)) \
    F(const char *, GameControllerName, (SDL_GameController *)) \
    F(uint8_t, GameControllerGetButton, (SDL_GameController *, int)) \
    F(int16_t, GameControllerGetAxis, (SDL_GameController *, int)) \
    F(void, GameControllerUpdate, (void)) \
    F(int, GameControllerEventState, (int)) \
    F(int, JoystickEventState, (int)) \
    F(int, GameControllerHasRumble, (SDL_GameController *)) \
    F(int, GameControllerRumble, (SDL_GameController *, uint16_t, uint16_t, uint32_t))
typedef struct {
    #define DECL(ret, name, args) ret (*name) args;
    SDL_API(DECL)
    #undef DECL
} sdl_api;
typedef struct { uint64_t buttons; int32_t lx, ly, rx, ry; } pad_state;
typedef struct {
    SDL_GameController *controller;
    int32_t id;
    int player, rumble;
    uint16_t low, high;
    uint64_t rumble_time, rumble_retry;
} pad_device;
struct dm_sdl_pad {
    void *library;
    sdl_api api;
    dm_sdl_pad_sink sink;
    pad_slots slots;
    pad_device devices[DM_PLAYERS];
    pad_state published[DM_PLAYERS];
    uint32_t physical, players;
    int merge, active, keyboard, rumble_errors;
};

static void publish(dm_sdl_pad *p, int player, pad_state s) {
    p->published[player] = s;
    p->sink.state(p->sink.context, player, s.buttons, s.lx, s.ly, s.rx, s.ry);
}
static void route(dm_sdl_pad *p, int player, int on) {
    if (p->sink.rumble_route) p->sink.rumble_route(p->sink.context, player, on);
}
static int32_t stick(int16_t value, int invert) {
    int32_t v = value; if (invert) v = -v;
    return v < -DM_STICK_MAX ? -DM_STICK_MAX : v > DM_STICK_MAX ? DM_STICK_MAX : v;
}
static pad_state read_pad(dm_sdl_pad *p, pad_device *d) {
    static const uint64_t bits[BUTTON_COUNT] = {
        DM_BTN_B, DM_BTN_A, DM_BTN_Y, DM_BTN_X, DM_BTN_MINUS, 0, DM_BTN_PLUS, DM_BTN_LS, DM_BTN_RS,
        DM_BTN_L, DM_BTN_R, DM_BTN_UP, DM_BTN_DOWN, DM_BTN_LEFT, DM_BTN_RIGHT
    };
    pad_state s = {0};
    for (int i = 0; i < BUTTON_COUNT; i++) if (p->api.GameControllerGetButton(d->controller, i)) s.buttons |= bits[i];
    if (p->api.GameControllerGetAxis(d->controller, AX_TRIGGERLEFT) > DM_STICK_MAX / 2) s.buttons |= DM_BTN_ZL;
    if (p->api.GameControllerGetAxis(d->controller, AX_TRIGGERRIGHT) > DM_STICK_MAX / 2) s.buttons |= DM_BTN_ZR;
    s.lx = stick(p->api.GameControllerGetAxis(d->controller, AX_LEFTX), 0);
    s.ly = stick(p->api.GameControllerGetAxis(d->controller, AX_LEFTY), 1); // SDL Y points down; Npad Y points up
    s.rx = stick(p->api.GameControllerGetAxis(d->controller, AX_RIGHTX), 0);
    s.ry = stick(p->api.GameControllerGetAxis(d->controller, AX_RIGHTY), 1);
    return s;
}
static void combine(pad_state *a, pad_state b) {
    a->buttons |= b.buttons;
    #define AXIS(x) if (abs(b.x) > abs(a->x)) a->x = b.x
    AXIS(lx); AXIS(ly); AXIS(rx); AXIS(ry);
    #undef AXIS
}
static void close_pad(dm_sdl_pad *p, pad_device *d) {
    if (d->rumble) p->api.GameControllerRumble(d->controller, 0, 0, 0);
    if (!p->merge) route(p, d->player, 0);
    pad_slot_free(&p->slots, d);
    p->api.GameControllerClose(d->controller);
    memset(d, 0, sizeof *d);
}
dm_sdl_pad *dm_sdl_pad_create(dm_sdl_pad_sink sink, int merge, int keyboard_own, const char *library, char *error, size_t cap) {
    dm_sdl_pad *p = calloc(1, sizeof *p);
    if (!p) { snprintf(error, cap, "out of memory"); return NULL; }
    if (!sink.state || !sink.players) { snprintf(error, cap, "missing input sink"); free(p); return NULL; }
    p->sink = sink; p->merge = merge != 0;
    if (library) p->library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    else {
        const char *paths[] = {"/opt/homebrew/lib/libSDL2.dylib", "/usr/local/lib/libSDL2.dylib", "libSDL2.dylib"};
        for (unsigned i = 0; !p->library && i < sizeof paths / sizeof *paths; i++) p->library = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (!p->library) { snprintf(error, cap, "cannot load SDL2: %s", dlerror()); goto fail; }
    #define LOAD(ret, name, args) \
        p->api.name = dlsym(p->library, "SDL_" #name); \
        if (!p->api.name) { snprintf(error, cap, "SDL2 is missing SDL_" #name); goto fail; }
    SDL_API(LOAD)
    #undef LOAD
    // Explicitly selected alternative: do not silently go through the same Apple decoder, or let labels flip Nintendo's face buttons twice.
    p->api.SetHintWithPriority("SDL_JOYSTICK_MFI", "0", SDL_HINT_OVERRIDE);
    p->api.SetHintWithPriority("SDL_JOYSTICK_HIDAPI", "1", SDL_HINT_OVERRIDE);
    p->api.SetHintWithPriority("SDL_JOYSTICK_HIDAPI_SWITCH", "1", SDL_HINT_OVERRIDE);
    p->api.SetHintWithPriority("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0", SDL_HINT_OVERRIDE);
    p->api.SetHintWithPriority("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1", SDL_HINT_OVERRIDE);
    if (p->api.InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        snprintf(error, cap, "SDL2 init: %s", p->api.GetError()); goto fail;
    }
    p->api.GameControllerEventState(0); p->api.JoystickEventState(0); // polling only; AppKit retains its event loop
    if (!p->merge && keyboard_own) { p->keyboard = 1; pad_slot_take(&p->slots, &p->keyboard, "keyboard"); }
    return p;
fail:
    if (p->library) dlclose(p->library);
    free(p); return NULL;
}
void dm_sdl_pad_poll(dm_sdl_pad *p, int active) {
    if (!p) return;
    p->api.GameControllerUpdate();
    int topology = 0;
    for (int i = 0; i < DM_PLAYERS; i++) {
        pad_device *d = &p->devices[i];
        if (d->controller && !p->api.GameControllerGetAttached(d->controller)) {
            fprintf(stderr, "SDL controller disconnected: player %d\n", d->player + 1);
            close_pad(p, d); topology = 1;
        }
    }
    for (int index = 0, count = p->api.NumJoysticks(); index < count; index++) {
        if (!p->api.IsGameController(index)) continue;
        int32_t id = p->api.JoystickGetDeviceInstanceID(index);
        if (id < 0) continue;
        pad_device *free_device = NULL; int held = 0;
        for (int j = 0; j < DM_PLAYERS; j++) {
            pad_device *d = &p->devices[j];
            if (d->controller && d->id == id) held = 1;
            if (!d->controller && !free_device) free_device = d;
        }
        if (held || !free_device || (!p->merge && pad_slot_mask(&p->slots) == (1u << DM_PLAYERS) - 1)) continue;
        pad_device *d = free_device;
        d->controller = p->api.GameControllerOpen(index);
        if (!d->controller) continue;
        const char *name = p->api.GameControllerName(d->controller);
        int slot = pad_slot_take(&p->slots, d, name);
        if (slot < 0) { p->api.GameControllerClose(d->controller); d->controller = NULL; continue; }
        d->id = id; d->player = p->merge ? 0 : slot; d->rumble = p->api.GameControllerHasRumble(d->controller);
        if (!p->merge) route(p, d->player, d->rumble);
        fprintf(stderr, "SDL controller '%s' connected: player %d\n", name ? name : "?", d->player + 1); topology = 1;
    }
    pad_state states[DM_PLAYERS] = {0}; uint32_t physical = 0; int rumble = 0;
    for (int i = 0; i < DM_PLAYERS; i++) {
        pad_device *d = &p->devices[i]; if (!d->controller) continue;
        physical |= 1u << d->player; rumble |= d->rumble;
        if (active) combine(&states[d->player], read_pad(p, d));
    }
    if (p->merge && topology) route(p, 0, rumble);
    uint32_t players = physical | (p->keyboard ? 1u : 0);
    if (players != p->players) { p->players = players; p->sink.players(p->sink.context, players); }
    // Publish changes, releases and focus resumes only: an idle poll must not cancel the DM_KEYS/DM_CTL input that shares this path.
    uint32_t affected = physical | p->physical;
    for (int player = 0; player < DM_PLAYERS; player++) if (affected >> player & 1) {
        if (memcmp(&p->published[player], &states[player], sizeof(pad_state)) ||
            (active && !p->active && (physical >> player & 1)) ||
            ((active ? physical ^ p->physical : p->physical & ~physical) >> player & 1))
            publish(p, player, states[player]);
    }
    if (!active && p->active) for (int player = 0; player < DM_PLAYERS; player++) dm_sdl_pad_rumble(p, player, 0, 0);
    p->physical = physical; p->active = active != 0;
}
void dm_sdl_pad_rumble(dm_sdl_pad *p, int player, uint16_t low, uint16_t high) {
    if (!p || player < 0 || player >= DM_PLAYERS) return;
    if (!p->active) low = high = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    for (int i = 0; i < DM_PLAYERS; i++) {
        pad_device *d = &p->devices[i];
        if (!d->controller || d->player != player || !d->rumble) continue;
        if ((low || high) && now < d->rumble_retry) continue;
        // Hardware expires after a second even if the process exits; refresh a persistent nonzero effect twice a second.
        if (d->low == low && d->high == high && (!(low || high) || now - d->rumble_time < 500000000)) continue;
        int result = p->api.GameControllerRumble(d->controller, low, high, low || high ? 1000 : 0);
        d->low = low; d->high = high; d->rumble_time = now;
        d->rumble_retry = result < 0 ? now + 500000000 : 0;
        if (result < 0 && ++p->rumble_errors <= 8) fprintf(stderr, "[RED] SDL rumble player %d: %s\n", player + 1, p->api.GetError());
    }
}
void dm_sdl_pad_destroy(dm_sdl_pad *p) {
    if (!p) return;
    for (int i = 0; i < DM_PLAYERS; i++) if (p->devices[i].controller) close_pad(p, &p->devices[i]);
    for (int player = 0; player < DM_PLAYERS; player++) if (p->physical >> player & 1) publish(p, player, (pad_state){0});
    if (p->merge) route(p, 0, 0);
    if (p->players) p->sink.players(p->sink.context, 0);
    p->api.QuitSubSystem(SDL_INIT_GAMECONTROLLER); dlclose(p->library); free(p);
}
