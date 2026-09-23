#include "hid_state.h"
#include "hid.h"
#include "button_action_backend.h"
#include "button_action.h"
#include <string.h>

namespace {
uint8_t mouseButtons, modifiers, keys[6];
bool linked = false, neutralMouse = true, neutralKeyboard = true;
// Preserve press/release transitions while endpoints are busy. Overload collapses
// to neutral then current physical state, so a release can never be lost forever.
struct Mouse { uint8_t buttons; int8_t x, y, wheel, pan; };
struct Keyboard { uint8_t modifiers, keys[6]; };
template<class T> struct Queue {
  T data[128]; unsigned head = 0, count = 0;
  void clear() { head = count = 0; }
  bool push(const T &v) {
    if (count == 128) return false;
    data[(head + count++) % 128] = v; return true;
  }
  T &front() { return data[head]; }
  void pop() { head = (head + 1) % 128; --count; }
};
Queue<Mouse> mouse;
Queue<Keyboard> keyboard;
void mouseState(int8_t x = 0, int8_t y = 0, int8_t wheel = 0, int8_t pan = 0) {
  if (!linked) return;
  if (!mouse.push({mouseButtons, x, y, wheel, pan})) {
    mouse.clear(); neutralMouse = true; mouse.push({mouseButtons, 0, 0, 0, 0});
  }
}
void keyboardState() {
  if (!linked) return;
  Keyboard state{modifiers, {0}}; memcpy(state.keys, keys, sizeof(keys));
  if (!keyboard.push(state)) { keyboard.clear(); neutralKeyboard = true; keyboard.push(state); }
}
}
void buttonActionMouse(uint8_t code, bool pressed) {
  // Middle remains owned/refcounted by the common engine, but is a scroll
  // modifier only. Never trigger the host's middle-click/autoscroll behavior.
  if (code == static_cast<uint8_t>(MouseButtonCode::Middle)) return;
  uint8_t mask = code == 1 ? 1 : 2;
  if (pressed) mouseButtons |= mask; else mouseButtons &= (uint8_t)~mask;
  mouseState();
}
void buttonActionModifier(uint8_t bit, bool pressed) {
  if (bit >= 4) return;
  if (pressed) modifiers |= 1u << bit; else modifiers &= (uint8_t)~(1u << bit);
  keyboardState();
}
void buttonActionKey(uint8_t usage, bool pressed) {
  if (!supportedKeyUsage(usage)) return;
  if (pressed) {
    for (uint8_t k : keys) if (k == usage) return;
    for (auto &k : keys) if (!k) { k = usage; break; }
  } else { for (auto &k : keys) if (k == usage) k = 0; }
  keyboardState();
}
extern "C" void redpoint_hid_motion(int8_t x, int8_t y) { mouseState(x, y); }
extern "C" void redpoint_hid_scroll(int8_t wheel, int8_t pan) { mouseState(0, 0, wheel, pan); }
extern "C" void redpoint_hid_current_mouse(uint8_t report[5]) {
  memset(report, 0, 5); report[0] = mouseButtons;
}
extern "C" void redpoint_hid_current_keyboard(uint8_t report[8]) {
  report[0] = modifiers; report[1] = 0; memcpy(report + 2, keys, 6);
}
extern "C" void redpoint_hid_service(bool ready) {
  if (!ready) {
    linked = false; mouse.clear(); keyboard.clear(); neutralMouse = neutralKeyboard = true;
    return;
  }
  if (!linked) { linked = true; mouseState(); keyboardState(); }
  if (neutralMouse) {
    if (redpoint_mouse_send(0, 0, 0, 0, 0)) neutralMouse = false;
  } else if (mouse.count) {
    const auto &r = mouse.front();
    if (redpoint_mouse_send(r.buttons, r.x, r.y, r.wheel, r.pan)) mouse.pop();
  }
  if (neutralKeyboard) {
    const uint8_t empty[6] = {};
    if (redpoint_keyboard_send(0, empty)) neutralKeyboard = false;
  } else if (keyboard.count) {
    const auto &r = keyboard.front();
    if (redpoint_keyboard_send(r.modifiers, r.keys)) keyboard.pop();
  }
}
