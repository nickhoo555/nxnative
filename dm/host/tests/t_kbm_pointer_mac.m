// Reproduce a Mac pointer that supplies AppKit events but no GCMouse device.
#include "../front_kbm_mac.m"
#import <objc/runtime.h>
#include <assert.h>

uint64_t front_btn;
int front_input_on = 1;
static int32_t dx_total, dy_total;
void front_push(void) {}
int dm_mouse_camera(void) { return 1; }
int dm_mouse_camera_kind(void) { return 1; }
double dm_mouse_camera_idle(void) { return 0; }
void dm_mouse_move(int32_t dx, int32_t dy) { dx_total += dx; dy_total += dy; }
static NSEvent *(^pointer_handler)(NSEvent *);
static int raw_available;
static NSArray *no_mice(id self, SEL cmd) { (void)self; (void)cmd; return raw_available ? @[[NSNull null]] : @[]; }
static id capture_monitor(id self, SEL cmd, NSEventMask mask, NSEvent *(^handler)(NSEvent *)) {
    (void)self; (void)cmd;
    if (mask & NSEventMaskMouseMoved) pointer_handler = [handler copy];
    return [NSObject new];
}
@interface PointerFixture : NSEvent @end
@implementation PointerFixture
- (NSEventType)type { return NSEventTypeMouseMoved; }
- (CGFloat)deltaX { return 12; }
- (CGFloat)deltaY { return -7; }
@end
int main(void) { @autoreleasepool {
    Method mice = class_getClassMethod(GCMouse.class, @selector(mice));
    IMP old_mice = method_setImplementation(mice, (IMP)no_mice);
    Method monitor = class_getClassMethod(NSEvent.class, @selector(addLocalMonitorForEventsMatchingMask:handler:));
    IMP old_monitor = method_setImplementation(monitor, (IMP)capture_monitor);
    kbm_start();
    if (!pointer_handler) { fputs("FAIL: no AppKit pointer route when GCMouse.mice is empty\n", stderr); return 1; }
    PointerFixture *event = [PointerFixture new];
    pointer_handler(event); assert(dx_total == 0 && dy_total == 0); // not captured
    g_mouse_cap = 1;
    pointer_handler(event); assert(dx_total == 12 && dy_total == 7);
    raw_available = 1; pointer_handler(event); assert(dx_total == 12 && dy_total == 7); // no double delivery
    raw_available = 0;
    front_input_on = 0; pointer_handler(event); assert(dx_total == 12 && dy_total == 7);
    g_mouse_cap = 0; front_input_on = 1;
    method_setImplementation(monitor, old_monitor); method_setImplementation(mice, old_mice);
    puts("AppKit-only pointer: camera deltas, vertical direction, capture and focus gates passed");
} return 0; }
