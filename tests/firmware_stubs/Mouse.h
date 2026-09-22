#pragma once
#include <stdint.h>
#include <vector>
#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
struct MockMouse {
  uint8_t held = 0;
  std::vector<int> events;
  void press(uint8_t mask) { held |= mask; events.push_back(mask); }
  void release(uint8_t mask) { held &= ~mask; events.push_back(-int(mask)); }
};
extern MockMouse Mouse;
