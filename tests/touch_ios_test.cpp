#include "input/touch.h"
#include "input/mouse.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

// Test time mocking
static time_millis s_mock_time = 1000;

time_millis time_get_millis(void) {
    return s_mock_time;
}

void time_set_millis(time_millis m) {
    s_mock_time = m;
}

// Mock platform and screen stubs
void platform_touch_reset() {}
void platform_t::screen_warp_mouse(int *x, int *y) {}
platform_t platform;

#include "graphics/screen.h"
screen_t g_screen = {1024, 768, {0, 0}};

#define RUN_TEST(fn) do { \
    printf("Running %s... ", #fn); \
    fn(); \
    printf("PASSED\n"); \
} while(0)

static void test_single_tap_left_click() {
    touch_clear_all();
    touch_set_mode(TOUCH_MODE_DIRECT);
    s_mock_time = 1000;
    
    // Tap starts at (100, 200)
    int idx = touch_create({100, 200}, s_mock_time);
    assert(idx >= 0);
    assert(touch_in_use(idx));
    
    // Process frame while finger is touching
    s_mock_time = 1050;
    touch_to_mouse();
    assert(mouse::get().x == 100);
    assert(mouse::get().y == 200);
    assert(!mouse::get().left.is_down);
    
    // Finger released at 1150ms (< 300ms click threshold)
    s_mock_time = 1150;
    touch_end(idx, s_mock_time);
    assert(touch_was_click(get_earliest_touch()));
    
    // Frame where click is dispatched
    touch_to_mouse();
    assert(mouse::get().left.went_down == true);
    
    // Next frame: button is released
    s_mock_time = 1166;
    reset_touches(0);
    touch_to_mouse();
    assert(mouse::get().left.went_up == true);
    assert(!mouse::get().left.is_down);
}

static void test_long_press_right_click() {
    touch_clear_all();
    touch_set_mode(TOUCH_MODE_DIRECT);
    s_mock_time = 2000;
    
    // Touch down at (300, 400)
    int idx = touch_create({300, 400}, s_mock_time);
    assert(idx >= 0);
    
    // Frame at 2200ms (200ms elapsed, not yet long press)
    s_mock_time = 2200;
    touch_to_mouse();
    assert(!touch_poll_long_press());
    assert(!mouse::get().right.went_down);
    
    // Frame at 2360ms (360ms elapsed > 350ms LONG_PRESS_TIME)
    s_mock_time = 2360;
    touch_to_mouse();
    // touch_to_mouse called touch_poll_long_press which triggered right click
    assert(mouse::get().right.went_down == true);
    
    // Next frame: right click released
    s_mock_time = 2380;
    touch_to_mouse();
    assert(mouse::get().right.went_up == true);
    assert(!mouse::get().right.is_down);
    
    // Now user lifts finger at 2500ms
    s_mock_time = 2500;
    touch_end(idx, s_mock_time);
    // Since long press was handled, touch_was_click MUST be false (no spurious left click!)
    assert(!touch_was_click(get_earliest_touch()));
    
    touch_to_mouse();
    assert(!mouse::get().left.went_down);
}

static void test_drag_does_not_trigger_click_or_long_press() {
    touch_clear_all();
    touch_set_mode(TOUCH_MODE_DIRECT);
    s_mock_time = 3000;
    
    int idx = touch_create({100, 100}, s_mock_time);
    assert(idx >= 0);
    
    // User drags finger beyond NOT_MOVING_RANGE (> 24px)
    s_mock_time = 3100;
    touch_move(idx, {150, 100}, s_mock_time);
    touch_to_mouse();
    
    // Advance time past 350ms
    s_mock_time = 3400;
    touch_to_mouse();
    // Dragging must NOT trigger long press
    assert(!mouse::get().right.went_down);
    
    // Finger released
    s_mock_time = 3450;
    touch_end(idx, s_mock_time);
    // Dragging must NOT trigger tap click
    assert(!touch_was_click(get_earliest_touch()));
    touch_to_mouse();
    assert(!mouse::get().left.went_down);
}

static void test_two_finger_tap_right_click() {
    touch_clear_all();
    touch_set_mode(TOUCH_MODE_DIRECT);
    s_mock_time = 4000;
    
    int f1 = touch_create({200, 200}, s_mock_time);
    s_mock_time = 4020;
    int f2 = touch_create({260, 220}, s_mock_time);
    
    // Both fingers release quickly (< 300ms)
    s_mock_time = 4150;
    touch_end(f1, s_mock_time);
    touch_end(f2, s_mock_time);
    
    assert(touch_was_two_finger_tap());
    
    // Processing touch_to_mouse triggers right click
    touch_to_mouse();
    assert(mouse::get().right.went_down == true);
    
    // Next frame: released
    s_mock_time = 4170;
    touch_to_mouse();
    assert(mouse::get().right.went_up == true);
    assert(!mouse::get().right.is_down);
}

int main() {
    printf("=== PharaohPad Touch Input Verification Suite ===\n");
    RUN_TEST(test_single_tap_left_click);
    RUN_TEST(test_long_press_right_click);
    RUN_TEST(test_drag_does_not_trigger_click_or_long_press);
    RUN_TEST(test_two_finger_tap_right_click);
    printf("=== All Touch Input Tests Passed Successfully! ===\n");
    return 0;
}
