#include "button_action.h"
#include "keyboard_mapping.h"
#include <Mouse.h>
#include <string.h>
#include <stdio.h>

namespace {
uint8_t mouseOwners[4] = {};
uint8_t keyOwners[MAX_KEY_USAGE + 1] = {};
uint8_t modifierOwners[4] = {};

uint8_t mouseMask(uint8_t code) {
  switch (static_cast<MouseButtonCode>(code)) {
    case MouseButtonCode::Left: return MOUSE_LEFT;
    case MouseButtonCode::Middle: return MOUSE_MIDDLE;
    case MouseButtonCode::Right: return MOUSE_RIGHT;
  }
  return 0;
}
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

void pressButtonAction(ButtonAction &activeAction, const ButtonAction &configured) {
  if (activeAction.type != ActionType::Disabled || !validButtonAction(configured)) return;
  activeAction = configured;
  if (activeAction.type == ActionType::MouseButton) {
    if (mouseOwners[activeAction.code]++ == 0) Mouse.press(mouseMask(activeAction.code));
  } else if (activeAction.type == ActionType::KeyboardShortcut) {
    // Modifiers before key; three physical buttons mean at most three unique keys.
    for (uint8_t bit = 0; bit < 4; ++bit) {
      if ((activeAction.modifiers & (1 << bit)) && modifierOwners[bit]++ == 0) keyboardModifier(bit, true);
    }
    if (keyOwners[activeAction.code]++ == 0) keyboardUsage(activeAction.code, true);
  }
}

void releaseButtonAction(ButtonAction &activeAction) {
  if (validButtonAction(activeAction)) {
    if (activeAction.type == ActionType::MouseButton) {
      if (mouseOwners[activeAction.code] && --mouseOwners[activeAction.code] == 0) Mouse.release(mouseMask(activeAction.code));
    } else if (activeAction.type == ActionType::KeyboardShortcut) {
      // Key before modifiers. Never release another physical button's ownership.
      if (keyOwners[activeAction.code] && --keyOwners[activeAction.code] == 0) keyboardUsage(activeAction.code, false);
      for (uint8_t bit = 0; bit < 4; ++bit) {
        if ((activeAction.modifiers & (1 << bit)) && modifierOwners[bit] && --modifierOwners[bit] == 0) keyboardModifier(bit, false);
      }
    }
  }
  activeAction = DISABLED_ACTION;
}

bool mouseActionHeld(MouseButtonCode code) {
  const uint8_t index = static_cast<uint8_t>(code);
  return index >= 1 && index <= 3 && mouseOwners[index] != 0;
}
