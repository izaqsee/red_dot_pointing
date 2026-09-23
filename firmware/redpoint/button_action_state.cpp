#include "button_action.h"
#include "button_action_backend.h"

namespace {
uint8_t mouseOwners[4] = {};
uint8_t keyOwners[MAX_KEY_USAGE + 1] = {};
uint8_t modifierOwners[4] = {};

} // namespace

void pressButtonAction(ButtonAction &activeAction, const ButtonAction &configured) {
  if (activeAction.type != ActionType::Disabled || !validButtonAction(configured)) return;
  activeAction = configured;
  if (activeAction.type == ActionType::MouseButton) {
    if (mouseOwners[activeAction.code]++ == 0) buttonActionMouse(activeAction.code, true);
  } else if (activeAction.type == ActionType::KeyboardShortcut) {
    // Modifiers before key; three physical buttons mean at most three unique keys.
    for (uint8_t bit = 0; bit < 4; ++bit) {
      if ((activeAction.modifiers & (1 << bit)) && modifierOwners[bit]++ == 0) buttonActionModifier(bit, true);
    }
    if (keyOwners[activeAction.code]++ == 0) buttonActionKey(activeAction.code, true);
  }
}

void releaseButtonAction(ButtonAction &activeAction) {
  if (validButtonAction(activeAction)) {
    if (activeAction.type == ActionType::MouseButton) {
      if (mouseOwners[activeAction.code] && --mouseOwners[activeAction.code] == 0) buttonActionMouse(activeAction.code, false);
    } else if (activeAction.type == ActionType::KeyboardShortcut) {
      // Key before modifiers. Never release another physical button's ownership.
      if (keyOwners[activeAction.code] && --keyOwners[activeAction.code] == 0) buttonActionKey(activeAction.code, false);
      for (uint8_t bit = 0; bit < 4; ++bit) {
        if ((activeAction.modifiers & (1 << bit)) && modifierOwners[bit] && --modifierOwners[bit] == 0) buttonActionModifier(bit, false);
      }
    }
  }
  activeAction = DISABLED_ACTION;
}

bool mouseActionHeld(MouseButtonCode code) {
  const uint8_t index = static_cast<uint8_t>(code);
  return index >= 1 && index <= 3 && mouseOwners[index] != 0;
}
