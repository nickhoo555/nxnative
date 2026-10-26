// A small SDL2 ABI fixture: the production plugin is loaded normally, without hardware or SDL installed.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int connected, id; char name[64]; int16_t axes[6]; uint8_t buttons[15]; int rumble; uint16_t low, high; } FakePad;
static FakePad pads[12];
static int initialized, fail_init, open_count, close_count, updates, position_hint, mfi_hint, switch_hint;
void fake_reset(void) { memset(pads, 0, sizeof pads); initialized = fail_init = open_count = close_count = updates = position_hint = mfi_hint = switch_hint = 0; }
void fake_add(int i, const char *name) { pads[i] = (FakePad){.connected = 1, .id = i + 100, .rumble = 1}; strncpy(pads[i].name, name, sizeof pads[i].name - 1); }
void fake_remove(int i) { pads[i].connected = 0; }
void fake_axis(int i, int a, int16_t value) { pads[i].axes[a] = value; }
void fake_button(int i, int b, uint8_t value) { pads[i].buttons[b] = value; }
int fake_count(int what) { return what == 0 ? initialized : what == 1 ? open_count : what == 2 ? close_count : updates; }
int fake_hints(void) { return position_hint && mfi_hint && switch_hint; }
void fake_fail_init(void) { fail_init = 1; }
unsigned fake_rumble(int i) { return pads[i].low | (unsigned)pads[i].high << 16; }
int SDL_SetHintWithPriority(const char *name, const char *value, int priority) {
    (void)priority;
    if (!strcmp(name, "SDL_GAMECONTROLLER_USE_BUTTON_LABELS")) position_hint = !strcmp(value, "0");
    if (!strcmp(name, "SDL_JOYSTICK_MFI")) mfi_hint = !strcmp(value, "0");
    if (!strcmp(name, "SDL_JOYSTICK_HIDAPI_SWITCH")) switch_hint = !strcmp(value, "1");
    return 1;
}
int SDL_InitSubSystem(uint32_t flags) {
    (void)flags; if (fail_init) return -1; initialized++;
    const char *input = getenv("DM_TEST_SDL_INPUT");
    if (input) { fake_add(0, "Probe fixture"); pads[0].axes[0] = 24576; if (!strcmp(input, "both")) pads[0].axes[2] = 24576; }
    return 0;
}
void SDL_QuitSubSystem(uint32_t flags) { (void)flags; initialized--; }
const char *SDL_GetError(void) { return "fixture init failure"; }
int SDL_NumJoysticks(void) { int n = 0; for (int i = 0; i < 12; i++) n += pads[i].connected; return n; }
static FakePad *at(int index) { for (int i = 0; i < 12; i++) if (pads[i].connected && index-- == 0) return &pads[i]; return 0; }
int SDL_IsGameController(int index) { return at(index) != 0; }
int32_t SDL_JoystickGetDeviceInstanceID(int index) { FakePad *p = at(index); return p ? p->id : -1; }
void *SDL_GameControllerOpen(int index) { open_count++; return at(index); }
void SDL_GameControllerClose(void *pad) { (void)pad; close_count++; }
int SDL_GameControllerGetAttached(FakePad *pad) { return pad->connected; }
const char *SDL_GameControllerName(FakePad *pad) { return pad->name; }
uint8_t SDL_GameControllerGetButton(FakePad *pad, int button) { return pad->buttons[button]; }
#ifndef OMIT_AXIS
int16_t SDL_GameControllerGetAxis(FakePad *pad, int axis) { return pad->axes[axis]; }
#endif
void SDL_GameControllerUpdate(void) { updates++; }
int SDL_GameControllerEventState(int state) { return state; }
int SDL_JoystickEventState(int state) { return state; }
int SDL_GameControllerHasRumble(FakePad *pad) { return pad->rumble; }
int SDL_GameControllerRumble(FakePad *pad, uint16_t low, uint16_t high, uint32_t ms) { (void)ms; pad->low = low; pad->high = high; return 0; }
