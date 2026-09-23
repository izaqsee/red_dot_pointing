#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void redpoint_hid_motion(int8_t x, int8_t y);
void redpoint_hid_scroll(int8_t wheel, int8_t pan);
void redpoint_hid_service(bool link_ready);
void redpoint_hid_current_mouse(uint8_t report[5]);
void redpoint_hid_current_keyboard(uint8_t report[8]);
#ifdef __cplusplus
}
#endif
