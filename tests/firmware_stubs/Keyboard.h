#pragma once
#include <stdint.h>
#include <stddef.h>
#include <set>
#include <vector>
#include <cassert>
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83
#define KEY_RETURN 0xB0
struct MockKeyboard {
  uint8_t modifiers = 0;
  std::set<uint8_t> keys;
  std::vector<int> events;
  size_t press(uint8_t code) {
    assert(code >= 128); // Passing HID usage directly would invoke ASCII conversion.
    if (code >= 136) keys.insert(code - 136);
    else modifiers |= 1 << (code - 128);
    events.push_back(code);
    return 1;
  }
  size_t release(uint8_t code) {
    assert(code >= 128);
    if (code >= 136) keys.erase(code - 136);
    else modifiers &= ~(1 << (code - 128));
    events.push_back(-int(code));
    return 1;
  }
};
extern MockKeyboard Keyboard;
