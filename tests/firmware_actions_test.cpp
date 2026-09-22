#include "button_action.h"
#include "keyboard_mapping.h"
#include <Mouse.h>
#include <Keyboard.h>
#include <cassert>
#include <cstring>
#include <iostream>

MockMouse Mouse;
MockKeyboard Keyboard;

void runActionTests() {
  ButtonAction action;
  char text[ACTION_TEXT_CAPACITY];
  for (const char *canonical : {"disabled", "mouse:left", "mouse:middle", "mouse:right", "key:03:17", "key:00:04", "key:0F:73"}) {
    assert(parseButtonAction(canonical, action));
    assert(validButtonAction(action));
    assert(formatButtonAction(action, text, sizeof(text)));
    assert(strcmp(canonical, text) == 0);
  }
  assert(parseButtonAction("key:0f:3a", action));
  assert(formatButtonAction(action, text, sizeof(text)) && strcmp(text, "key:0F:3A") == 0);
  assert(!formatButtonAction(action, text, 2));
  for (const char *invalid : {"mouse:back", "Mouse:left", "disabled:0", "key:10:17", "key:01:00", "key:01:E0", "key:00:32", "key:00:66", "key:00:FF", "key:03:GG", "key:3:17", "key:03:17extra", "key:03:17 ", "key:03:-1", "key:03:+1"}) {
    action = LEFT_ACTION;
    assert(!parseButtonAction(invalid, action));
    assert(action.type == ActionType::MouseButton && action.code == 1); // No partial mutation.
  }
  assert(!validButtonAction({static_cast<ActionType>(99), 0, 0}));
  assert(!validButtonAction({ActionType::Disabled, 1, 0}));
  assert(!validButtonAction({ActionType::Disabled, 0, 1}));
  assert(!validButtonAction({ActionType::MouseButton, 1, 1}));
  for (int usage = 0; usage <= 255; ++usage) {
    uint8_t mapped = 0;
    const bool expected = usage >= 4 && usage <= 0x73 && usage != 0x32 && usage != 0x66;
    assert(supportedKeyUsage(usage) == expected);
    assert(keyboardApiCode(usage, mapped) == expected);
    if (expected) assert(mapped == usage + 136);
  }

  ButtonAction left = DISABLED_ACTION, middle = DISABLED_ACTION, right = DISABLED_ACTION;
  for (auto code : {MouseButtonCode::Left, MouseButtonCode::Middle, MouseButtonCode::Right}) {
    ButtonAction configured = {ActionType::MouseButton, static_cast<uint8_t>(code), 0};
    pressButtonAction(left, configured);
    const auto events = Mouse.events.size();
    pressButtonAction(right, configured);
    assert(Mouse.events.size() == events && mouseActionHeld(code));
    configured = DISABLED_ACTION; // DeviceConfig may change while held.
    releaseButtonAction(left);
    assert(mouseActionHeld(code));
    releaseButtonAction(right);
    assert(!mouseActionHeld(code) && Mouse.held == 0);
    releaseButtonAction(right); // Repeated release cannot underflow.
  }
  const ButtonAction ctrlC = {ActionType::KeyboardShortcut, 0x06, 1};
  pressButtonAction(left, ctrlC);
  assert(Keyboard.events[0] == KEY_LEFT_CTRL && Keyboard.events[1] == 0x06 + 136);
  const auto count = Keyboard.events.size();
  pressButtonAction(right, ctrlC);
  assert(Keyboard.events.size() == count);
  releaseButtonAction(left);
  assert(Keyboard.modifiers == 1 && Keyboard.keys.count(0x06) == 1);
  releaseButtonAction(right);
  assert(Keyboard.modifiers == 0 && Keyboard.keys.empty());
  assert(Keyboard.events.back() == -KEY_LEFT_CTRL); // Key released before modifier.

  ButtonAction configured = {ActionType::KeyboardShortcut, 0x17, 3};
  pressButtonAction(left, configured);
  configured = MIDDLE_ACTION;
  assert(!mouseActionHeld(MouseButtonCode::Middle));
  pressButtonAction(right, ctrlC); // Shared Ctrl, independent Shift/T.
  releaseButtonAction(left);
  assert(Keyboard.modifiers == 1 && Keyboard.keys == std::set<uint8_t>{0x06});
  releaseButtonAction(right);
  assert(Keyboard.modifiers == 0 && Keyboard.keys.empty());
  pressButtonAction(left, configured);
  assert(mouseActionHeld(MouseButtonCode::Middle));
  configured = ctrlC;
  assert(mouseActionHeld(MouseButtonCode::Middle)); // Latched until release.
  releaseButtonAction(left);
  assert(!mouseActionHeld(MouseButtonCode::Middle));

  pressButtonAction(left, {ActionType::KeyboardShortcut, 0x17, 1});
  pressButtonAction(middle, {ActionType::KeyboardShortcut, 0x17, 2}); // Same key, overlapping ownership.
  pressButtonAction(right, {ActionType::KeyboardShortcut, 0x3e, 12});
  assert(Keyboard.modifiers == 15 && Keyboard.keys.size() == 2);
  releaseButtonAction(left);
  assert(Keyboard.modifiers == 14 && Keyboard.keys.count(0x17));
  releaseButtonAction(middle);
  assert(Keyboard.modifiers == 12 && Keyboard.keys == std::set<uint8_t>{0x3e});
  releaseButtonAction(right);
  assert(Keyboard.modifiers == 0 && Keyboard.keys.empty());
  const auto idleCount = Keyboard.events.size();
  pressButtonAction(left, DISABLED_ACTION);
  releaseButtonAction(left);
  assert(Keyboard.events.size() == idleCount && Mouse.held == 0);
  std::cout << "PASS: action validation/canonical text, API mapping, latched release, duplicate keys/mouse and overlapping modifiers\n";
}
