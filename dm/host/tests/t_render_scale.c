// Exercise the production loader's render-scale validation and state-page write without game files.
#include "../loader.c"
#include <assert.h>

struct dm_flags g_flag;
uint64_t now_ns(void) { return 0; }
void gate_register(uint64_t a, const char *k, int n, uint64_t o) { (void)a; (void)k; (void)n; (void)o; }
void hlecount_seal(void) {}
void *hlecount_wrap(const char *kind, const char *name, void *fn) { (void)kind; (void)name; return fn; }
uint64_t resolve_import(module_t *m, const char *name) { (void)m; (void)name; return 0; }
int hle_implements(const char *name) { (void)name; return 0; }

static void check(const char *value, float expected) {
    if (value) assert(!setenv("DM_RENDER_SCALE", value, 1)); else unsetenv("DM_RENDER_SCALE");
    memset(dm_fps_state, 0, 0x100);
    dm_fps_state_apply();
    float actual; memcpy(&actual, dm_fps_state + 0x10, sizeof actual);
    assert(actual == expected);
    memcpy(&actual, dm_fps_state, sizeof actual);
    assert(actual == 0);   // Render scale is independent of the frame-rate override.
}

int main(void) {
    static uint8_t state[0x100] __attribute__((aligned(16)));
    dm_fps_state = state; g_fps_mod = "test";
    assert(!setenv("DM_FPS_TARGET", "0", 1));
    check(NULL, 1); check("0.5", 0.5f); check("1", 1);
    check("1.6", 1.6f); check("2.4", 2.4f); check("3", 3);
    const char *invalid[] = {"", "invalid", "nan", "inf", "-inf", "0.49", "3.01", "2.4junk"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) check(invalid[i], 1);
    unsetenv("DM_RENDER_SCALE"); unsetenv("DM_FPS_TARGET"); dm_fps_state = NULL;
    puts("Mac render scale: defaults, supersampling, bounds, nonfinite values and state page: OK");
    return 0;
}
