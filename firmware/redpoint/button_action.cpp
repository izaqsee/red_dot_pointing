#include "button_action_backend.h"
#include "keyboard_mapping.h"
#include <Mouse.h>
void buttonActionMouse(uint8_t code, bool pressed) {
  const uint8_t mask = code == 1 ? MOUSE_LEFT : code == 2 ? MOUSE_MIDDLE : MOUSE_RIGHT;
  if (pressed) Mouse.press(mask); else Mouse.release(mask);
}
void buttonActionKey(uint8_t code, bool pressed) { keyboardUsage(code, pressed); }
void buttonActionModifier(uint8_t bit, bool pressed) { keyboardModifier(bit, pressed); }
