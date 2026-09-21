#include "config_storage.h"
#include "config_record.h"

#include <EEPROM.h>
#include <string.h>

namespace {
bool initialized = false;
bool wroteFlash = false;

void readRecord(uint8_t *record) {
  for (size_t i = 0; i < CONFIG_RECORD_SIZE; ++i) record[i] = EEPROM.read(i);
}
} // namespace

bool loadDeviceConfig(DeviceConfig &value) {
  // Core 6.1.0 rounds this to one 256-byte RAM buffer, allocated once at boot.
  EEPROM.begin(CONFIG_RECORD_SIZE);
  initialized = EEPROM.length() >= CONFIG_RECORD_SIZE;
  value = DEFAULT_CONFIG;
  if (!initialized) return false;
  uint8_t record[CONFIG_RECORD_SIZE];
  readRecord(record);
  return decodeConfigRecord(record, value);
}

ConfigSaveResult saveDeviceConfig(const DeviceConfig &value) {
  uint8_t record[CONFIG_RECORD_SIZE];
  if (!encodeConfigRecord(value, record)) return ConfigSaveResult::InvalidConfig;
  if (!initialized) return ConfigSaveResult::StorageError;

  // Compare against Flash, not only the boot-time RAM copy, even for a no-op SAVE.
  EEPROM.begin(CONFIG_RECORD_SIZE);
  uint8_t previous[CONFIG_RECORD_SIZE];
  readRecord(previous);
  if (memcmp(previous, record, CONFIG_RECORD_SIZE) == 0) return ConfigSaveResult::Saved;

  for (size_t i = 0; i < CONFIG_RECORD_SIZE; ++i) EEPROM.write(i, record[i]);
  wroteFlash = true; // Also resync after a failed/partial write.
  const bool committed = EEPROM.commit();

  // Core commit() has no hardware verification. begin(same size) reloads Flash,
  // clears dirty state and reuses its buffer. Do not use end(): it also commits.
  EEPROM.begin(CONFIG_RECORD_SIZE);
  uint8_t actual[CONFIG_RECORD_SIZE];
  readRecord(actual);
  DeviceConfig decoded;
  if (!committed || memcmp(actual, record, CONFIG_RECORD_SIZE) != 0 ||
      !decodeConfigRecord(actual, decoded)) return ConfigSaveResult::StorageError;
  return ConfigSaveResult::Saved;
}

bool takeConfigFlashWrite() {
  const bool result = wroteFlash;
  wroteFlash = false;
  return result;
}
