// Arduino storage semantics, using an explicit Pico flash HAL instead of EEPROM.
#include "config_storage.h"
#include "config_record.h"
#include "platform_io.h"
#include <string.h>
namespace { bool initialized, wroteFlash; }
bool loadDeviceConfig(DeviceConfig &value) {
  initialized = redpoint_storage_init();
  wroteFlash = false;
  value = DEFAULT_CONFIG;
  uint8_t record[CONFIG_RECORD_SIZE];
  return initialized && redpoint_storage_read(record, sizeof(record)) && decodeConfigRecord(record, value);
}
ConfigSaveResult saveDeviceConfig(const DeviceConfig &value) {
  uint8_t record[CONFIG_RECORD_SIZE], previous[CONFIG_RECORD_SIZE], actual[CONFIG_RECORD_SIZE];
  if (!encodeConfigRecord(value, record)) return ConfigSaveResult::InvalidConfig;
  if (!initialized || !redpoint_storage_read(previous, sizeof(previous))) return ConfigSaveResult::StorageError;
  if (!memcmp(previous, record, sizeof(record))) return ConfigSaveResult::Saved;
  // Even a failed/partial attempt may have masked PS/2 IRQs: always request resync.
  wroteFlash = true;
  bool committed = redpoint_storage_commit(record, sizeof(record));
  DeviceConfig decoded;
  if (!committed || !redpoint_storage_read(actual, sizeof(actual)) ||
      memcmp(actual, record, sizeof(actual)) || !decodeConfigRecord(actual, decoded))
    return ConfigSaveResult::StorageError;
  return ConfigSaveResult::Saved;
}
bool takeConfigFlashWrite() { bool result = wroteFlash; wroteFlash = false; return result; }
