// Milestone B: shared, volatile configuration. No Flash or WS2812 hardware backend.
#include "config_platform.h"
#include "config.h"
#include "config_storage.h"
#include "status_led.h"

namespace {
bool boot = true, active = false, unsaved = false, saving = false, error = false;
uint32_t activityAt = 0, errorAt = 0;
}

bool loadDeviceConfig(DeviceConfig &value) {
  value = DEFAULT_CONFIG;
  return false; // No persisted record has been loaded.
}
ConfigSaveResult saveDeviceConfig(const DeviceConfig &value) {
  // Never report a successful save or advance the baseline without real storage.
  return validDeviceConfig(value) ? ConfigSaveResult::StorageError : ConfigSaveResult::InvalidConfig;
}
bool takeConfigFlashWrite() { return false; } // This target performs no Flash writes.

void statusLedBegin() { boot = true; active = unsaved = saving = error = false; }
void statusLedEndBoot() { boot = false; }
void statusLedActivity() { active = true; activityAt = redpoint_platform_millis(); }
void statusLedSetUnsaved(bool value) { unsaved = value; }
void statusLedSetSaving(bool value) { saving = value; }
void statusLedSignalError() { error = true; errorAt = redpoint_platform_millis(); }
StatusLedState statusLedState() {
  const uint32_t now = redpoint_platform_millis();
  if (active && uint32_t(now - activityAt) >= STATUS_CONNECTED_TIMEOUT_MS) active = false;
  if (error && uint32_t(now - errorAt) >= STATUS_ERROR_HOLD_MS) error = false;
  if (boot) return StatusLedState::Boot;
  if (error) return StatusLedState::Error;
  if (saving) return StatusLedState::Saving;
  if (unsaved) return StatusLedState::Unsaved;
  return active ? StatusLedState::Connected : StatusLedState::Normal;
}
void statusLedUpdate() { (void)statusLedState(); } // Logical status only; no physical LED.
bool statusLedReadyForCommand() { return !boot; } // No hardware latch to wait for.

extern "C" void redpoint_config_init(void) {
  statusLedBegin();
  loadDeviceConfig(config);
  configSetPersistentBaseline(config); // Defaults are the boot baseline, not a saved record.
  (void)takeConfigChange();
  statusLedEndBoot();
}
