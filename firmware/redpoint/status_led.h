#pragma once
#include <stdint.h>
constexpr uint8_t STATUS_LED_BRIGHTNESS = 8;
constexpr uint32_t STATUS_CONNECTED_TIMEOUT_MS = 6000;
constexpr uint32_t STATUS_ERROR_HOLD_MS = 2000;
constexpr uint32_t STATUS_SAVING_HOLD_MS = 150;
enum class StatusLedState { Boot, Normal, Connected, Unsaved, Saving, Error };
// Setup/main loop only. Never call from an ISR.
void statusLedBegin();
void statusLedEndBoot();
void statusLedActivity();
void statusLedSetUnsaved(bool value);
void statusLedSetSaving(bool value);
void statusLedSignalError();
StatusLedState statusLedState();
void statusLedUpdate();
// Yield serial polling during the hardware latch interval, without blocking HID.
bool statusLedReadyForCommand();
