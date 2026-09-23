#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { HID_MOUSE, HID_KEYBOARD };
bool redpoint_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);
bool redpoint_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);
void redpoint_hid_task(void);
#ifdef __cplusplus
}
#endif
