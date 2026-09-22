#pragma once
#include <stdint.h>
#include <stddef.h>

enum class ActionType : uint8_t { Disabled = 0, MouseButton = 1, KeyboardShortcut = 2 };
// Stored values, independent of the Mouse library's button masks.
enum class MouseButtonCode : uint8_t { Left = 1, Middle = 2, Right = 3 };
struct ButtonAction {
  ActionType type;
  uint8_t code;
  uint8_t modifiers; // Ctrl=1, Shift=2, Alt=4, GUI=8; upper bits reserved.
};
constexpr ButtonAction DISABLED_ACTION = {ActionType::Disabled, 0, 0};
constexpr ButtonAction LEFT_ACTION = {ActionType::MouseButton, 1, 0};
constexpr ButtonAction MIDDLE_ACTION = {ActionType::MouseButton, 2, 0};
constexpr ButtonAction RIGHT_ACTION = {ActionType::MouseButton, 3, 0};
constexpr size_t ACTION_TEXT_CAPACITY = 13; // longest: mouse:middle + NUL
constexpr uint8_t MAX_KEY_USAGE = 0x73;

bool supportedKeyUsage(uint8_t usage);
bool validButtonAction(const ButtonAction &action);
bool parseButtonAction(const char *text, ButtonAction &action);
bool formatButtonAction(const ButtonAction &action, char *text, size_t capacity);

// Main loop only. activeAction belongs to one physical button, not DeviceConfig.
void pressButtonAction(ButtonAction &activeAction, const ButtonAction &configured);
void releaseButtonAction(ButtonAction &activeAction);
bool mouseActionHeld(MouseButtonCode code);
