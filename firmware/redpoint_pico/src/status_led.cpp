// Same state/priority/timers as the Arduino reference, with a nonblocking PIO backend.
#include "status_led.h"
#include "platform_io.h"
namespace {
bool boot, active, unsaved, saving, savingHold, errorHold, ready, displayed;
uint32_t activityAt, savingAt, errorAt;
StatusLedState previous;
}
void statusLedBegin() {
  boot = true;
  active = unsaved = saving = savingHold = errorHold = displayed = false;
  ready = redpoint_led_init();
  statusLedUpdate();
}
void statusLedEndBoot() { boot = false; statusLedUpdate(); }
void statusLedActivity() { active = true; activityAt = redpoint_platform_millis(); }
void statusLedSetUnsaved(bool value) { unsaved = value; }
void statusLedSetSaving(bool value) {
  saving = value;
  if (value) { savingHold = true; savingAt = redpoint_platform_millis(); }
  statusLedUpdate(); // Submit purple BEFORE entering synchronous Flash commit.
}
void statusLedSignalError() { errorHold = true; errorAt = redpoint_platform_millis(); }
StatusLedState statusLedState() {
  const uint32_t now = redpoint_platform_millis();
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
  if (!ready || (displayed && state == previous) || (displayed && !redpoint_led_ready())) return;
  uint8_t r = 0, g = 0, b = 0;
  switch (state) {
    case StatusLedState::Boot: r = g = b = 255; break;
    case StatusLedState::Normal: b = 255; break;
    case StatusLedState::Connected: g = 255; break;
    case StatusLedState::Unsaved: r = g = 255; break;
    case StatusLedState::Saving: r = b = 255; break;
    case StatusLedState::Error: r = 255; break;
  }
  redpoint_led_write(r, g, b);
  previous = state;
  displayed = true;
}

bool statusLedReadyForCommand() { return !ready || redpoint_led_ready(); }
