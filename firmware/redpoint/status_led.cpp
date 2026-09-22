#include "status_led.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
namespace {
Adafruit_NeoPixel pixel(1, 23, NEO_GRB + NEO_KHZ800);
bool boot, active, unsaved, saving, savingHold, errorHold, ready, displayed;
uint32_t activityAt, savingAt, errorAt;
StatusLedState previous;
}
void statusLedBegin() {
  boot = true;
  active = unsaved = saving = savingHold = errorHold = displayed = false;
  ready = pixel.begin();
  pixel.setBrightness(STATUS_LED_BRIGHTNESS);
  statusLedUpdate();
}
void statusLedEndBoot() { boot = false; statusLedUpdate(); }
void statusLedActivity() { active = true; activityAt = millis(); }
void statusLedSetUnsaved(bool value) { unsaved = value; }
void statusLedSetSaving(bool value) {
  saving = value;
  if (value) { savingHold = true; savingAt = millis(); }
  statusLedUpdate(); // Submit purple BEFORE entering synchronous Flash commit.
}
void statusLedSignalError() { errorHold = true; errorAt = millis(); }
StatusLedState statusLedState() {
  const uint32_t now = millis();
  if (active && uint32_t(now - activityAt) >= STATUS_CONNECTED_TIMEOUT_MS) active = false;
  if (errorHold && uint32_t(now - errorAt) >= STATUS_ERROR_HOLD_MS) errorHold = false;
  if (savingHold && uint32_t(now - savingAt) >= STATUS_SAVING_HOLD_MS) savingHold = false;
  if (boot) return StatusLedState::Boot;
  if (errorHold) return StatusLedState::Error;
  if (saving || savingHold) return StatusLedState::Saving;
  if (unsaved) return StatusLedState::Unsaved;
  return active ? StatusLedState::Connected : StatusLedState::Normal;
}
void statusLedUpdate() {
  const StatusLedState state = statusLedState();
  // canShow avoids the library's blocking latch wait on rapid transitions.
  if (!ready || (displayed && state == previous) || (displayed && !pixel.canShow())) return;
  uint8_t r = 0, g = 0, b = 0;
  switch (state) {
    case StatusLedState::Boot: r = g = b = 255; break;
    case StatusLedState::Normal: b = 255; break;
    case StatusLedState::Connected: g = 255; break;
    case StatusLedState::Unsaved: r = g = 255; break;
    case StatusLedState::Saving: r = b = 255; break;
    case StatusLedState::Error: r = 255; break;
  }
  pixel.setPixelColor(0, r, g, b);
  pixel.show();
  previous = state;
  displayed = true;
}

bool statusLedReadyForCommand() { return !ready || pixel.canShow(); }
