#include "keyboard_mapping.h"
#include "button_action.h"
#include <Keyboard.h>

namespace {
// HID_Keyboard::press/release subtract 136 for non-printing keys.
constexpr uint16_t NON_PRINTING_OFFSET = 136;
constexpr uint8_t MODIFIER_CODES[] = {KEY_LEFT_CTRL, KEY_LEFT_SHIFT, KEY_LEFT_ALT, KEY_LEFT_GUI};
static_assert(MAX_KEY_USAGE + NON_PRINTING_OFFSET <= 255, "Key API overflow");
static_assert(KEY_RETURN == 0x28 + NON_PRINTING_OFFSET, "Unsupported Keyboard API");
}

bool keyboardApiCode(uint8_t usage, uint8_t &apiCode) {
  if (!supportedKeyUsage(usage)) return false;
  apiCode = uint8_t(usage + NON_PRINTING_OFFSET);
  return true;
}
void keyboardUsage(uint8_t usage, bool pressed) {
  uint8_t code;
  if (!keyboardApiCode(usage, code)) return;
  if (pressed) Keyboard.press(code);
  else Keyboard.release(code);
}
void keyboardModifier(uint8_t bit, bool pressed) {
  if (bit >= 4) return;
  if (pressed) Keyboard.press(MODIFIER_CODES[bit]);
  else Keyboard.release(MODIFIER_CODES[bit]);
}
