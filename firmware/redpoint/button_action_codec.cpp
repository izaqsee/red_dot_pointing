#include "button_action.h"
#include <string.h>
#include <stdio.h>

namespace {
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
} // namespace

bool supportedKeyUsage(uint8_t usage) {
  // Keyboard page 0x07: standard keys through F24; no Power/Non-US #.
  // Keep in sync with the explicit KeyboardEvent.code table in shortcuts.js.
  return usage >= 0x04 && usage <= MAX_KEY_USAGE && usage != 0x32 && usage != 0x66;
}

bool validButtonAction(const ButtonAction &action) {
  switch (action.type) {
    case ActionType::Disabled: return action.code == 0 && action.modifiers == 0;
    case ActionType::MouseButton:
      return action.code >= 1 && action.code <= 3 && action.modifiers == 0;
    case ActionType::KeyboardShortcut:
      return (action.modifiers & 0xf0) == 0 && supportedKeyUsage(action.code);
  }
  return false;
}

bool parseButtonAction(const char *text, ButtonAction &action) {
  ButtonAction parsed = DISABLED_ACTION;
  if (strcmp(text, "disabled") == 0) parsed = DISABLED_ACTION;
  else if (strcmp(text, "mouse:left") == 0) parsed = LEFT_ACTION;
  else if (strcmp(text, "mouse:middle") == 0) parsed = MIDDLE_ACTION;
  else if (strcmp(text, "mouse:right") == 0) parsed = RIGHT_ACTION;
  else if (strlen(text) == 9 && strncmp(text, "key:", 4) == 0 && text[6] == ':') {
    const int m1 = hexDigit(text[4]), m2 = hexDigit(text[5]);
    const int k1 = hexDigit(text[7]), k2 = hexDigit(text[8]);
    if (m1 < 0 || m2 < 0 || k1 < 0 || k2 < 0) return false;
    parsed = {ActionType::KeyboardShortcut, uint8_t(k1 * 16 + k2), uint8_t(m1 * 16 + m2)};
  } else return false;
  if (!validButtonAction(parsed)) return false;
  action = parsed;
  return true;
}

bool formatButtonAction(const ButtonAction &action, char *text, size_t capacity) {
  if (!validButtonAction(action) || capacity < ACTION_TEXT_CAPACITY) return false;
  if (action.type == ActionType::Disabled) strcpy(text, "disabled");
  else if (action.type == ActionType::MouseButton) {
    strcpy(text, action.code == 1 ? "mouse:left" : action.code == 2 ? "mouse:middle" : "mouse:right");
  } else snprintf(text, capacity, "key:%02X:%02X", unsigned(action.modifiers), unsigned(action.code));
  return true;
}

