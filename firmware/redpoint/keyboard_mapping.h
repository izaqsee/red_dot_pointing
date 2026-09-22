#pragma once
#include <stdint.h>

// Philhower 6.1.0's non-printing-key API, not raw HID usages or ASCII.
bool keyboardApiCode(uint8_t usage, uint8_t &apiCode);
void keyboardUsage(uint8_t usage, bool pressed);
void keyboardModifier(uint8_t bit, bool pressed);
