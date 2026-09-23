#pragma once
#include <stdint.h>
// Output boundary for the common action latch/reference-counting engine.
void buttonActionMouse(uint8_t code, bool pressed);
void buttonActionKey(uint8_t usage, bool pressed);
void buttonActionModifier(uint8_t bit, bool pressed);
