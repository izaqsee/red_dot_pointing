#include "config.h"
#include "config_record.h"
#include "config_storage.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

MockEEPROM EEPROM;

bool equal(const DeviceConfig &a, const DeviceConfig &b) {
  return a.pointerSensitivity == b.pointerSensitivity && a.middleSensitivity == b.middleSensitivity &&
    a.invertX == b.invertX && a.invertY == b.invertY;
}

// Independent CRC fixture helper to test semantic rejection despite a valid CRC.
void repairCRC(uint8_t *record) {
  uint32_t crc = 0xffffffff;
  for (int i = 0; i < 20; ++i) {
    crc ^= record[i];
    for (int j = 0; j < 8; ++j) crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
  }
  crc ^= 0xffffffff;
  for (int i = 0; i < 4; ++i) record[20 + i] = (crc >> (8 * i)) & 255;
}

void expectFallback(const uint8_t *record) {
  DeviceConfig result = {9, 9, true, true};
  assert(!decodeConfigRecord(record, result));
  assert(equal(result, DEFAULT_CONFIG));
}

bool command(Stream &serial, const std::string &text) {
  serial.input = text;
  serial.position = 0;
  serial.output.clear();
  bool changed = false;
  while (serial.available()) changed = pollConfigSerial(serial) || changed;
  return changed;
}

int main() {
  const DeviceConfig custom = {1.25f, 0.35f, true, false};
  uint8_t record[CONFIG_RECORD_SIZE];
  assert(encodeConfigRecord(custom, record));
  assert(memcmp(record, "RPNT", 4) == 0);
  assert(record[4] == 1 && record[5] == 0 && record[6] == 24 && record[7] == 0);
  // IEEE-754 1.25 = 0x3fa00000, explicitly little endian.
  assert(record[8] == 0 && record[9] == 0 && record[10] == 0xa0 && record[11] == 0x3f);
  DeviceConfig decoded;
  assert(decodeConfigRecord(record, decoded) && equal(decoded, custom));
  uint8_t damaged[CONFIG_RECORD_SIZE];
  for (size_t offset = 0; offset < CONFIG_RECORD_SIZE; ++offset) {
    memcpy(damaged, record, sizeof(record));
    damaged[offset] ^= 1;
    expectFallback(damaged); // Includes every CRC byte and payload byte.
  }
  for (int offset : {0, 4, 6, 16, 17, 18, 19}) {
    memcpy(damaged, record, sizeof(record));
    damaged[offset] = 0xfe;
    repairCRC(damaged);
    expectFallback(damaged); // Magic, version, size, boolean encoding, reserved.
  }
  for (float bad : {-0.1f, 10.1f, std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
    for (int offset : {8, 12}) {
      memcpy(damaged, record, sizeof(record));
      uint32_t bits;
      memcpy(&bits, &bad, 4);
      for (int i = 0; i < 4; ++i) damaged[offset + i] = (bits >> (8 * i)) & 255;
      repairCRC(damaged);
      expectFallback(damaged);
    }
    DeviceConfig invalid = custom;
    invalid.pointerSensitivity = bad;
    assert(!validDeviceConfig(invalid));
    assert(!encodeConfigRecord(invalid, damaged));
    assert(saveDeviceConfig(invalid) == ConfigSaveResult::InvalidConfig);
    invalid = custom;
    invalid.middleSensitivity = bad;
    assert(!encodeConfigRecord(invalid, damaged));
  }
  assert(EEPROM.commits == 0 && EEPROM.writes == 0);
  assert(saveDeviceConfig(custom) == ConfigSaveResult::StorageError); // Not initialized.
  EEPROM.flash.fill(0xff);
  assert(!loadDeviceConfig(config) && equal(config, DEFAULT_CONFIG));
  assert(EEPROM.commits == 0 && EEPROM.writes == 0); // Boot never repairs Flash.
  for (size_t i = 0; i < CONFIG_RECORD_SIZE; ++i) EEPROM.flash[i] = record[i];
  assert(loadDeviceConfig(config) && equal(config, custom));

  Stream serial;
  assert(!command(serial, "SAVE\n"));
  assert(serial.output.find("\"command\":\"SAVE\"") != std::string::npos);
  assert(EEPROM.commits == 0); // Unchanged save doesn't wear Flash.
  assert(command(serial, "RESET\n"));
  assert(equal(config, DEFAULT_CONFIG));
  assert(EEPROM.commits == 0);
  assert(loadDeviceConfig(config) && equal(config, custom)); // RESET without SAVE.

  assert(command(serial, "SET middleSensitivity 0.25\n"));
  assert(EEPROM.commits == 0 && EEPROM.writes == 0);
  assert(!command(serial, "SAVE\n"));
  assert(EEPROM.commits == 1);
  assert(takeConfigFlashWrite() && !takeConfigFlashWrite());
  assert(serial.output.find("\"ok\":true,\"command\":\"SAVE\"") != std::string::npos);
  assert(loadDeviceConfig(config) && config.middleSensitivity == 0.25f);
  command(serial, "SAVE\n");
  assert(EEPROM.commits == 1 && !takeConfigFlashWrite());
  command(serial, "SAVE extra\n");
  assert(serial.output.find("INVALID_ARGUMENTS") != std::string::npos && EEPROM.commits == 1);

  config.pointerSensitivity = 11;
  command(serial, "SAVE\n");
  assert(serial.output.find("INVALID_CONFIG") != std::string::npos && EEPROM.commits == 1);
  config = DEFAULT_CONFIG;
  EEPROM.commitFails = true;
  command(serial, "SAVE\n");
  assert(serial.output.find("SAVE_FAILED") != std::string::npos);
  assert(takeConfigFlashWrite());
  assert(equal(config, DEFAULT_CONFIG)); // Failure doesn't alter runtime.
  EEPROM.commitFails = false;
  EEPROM.corruptCommit = true;
  command(serial, "SAVE\n");
  assert(serial.output.find("SAVE_FAILED") != std::string::npos); // Readback, not just commit return.
  assert(!loadDeviceConfig(config) && equal(config, DEFAULT_CONFIG));
  EEPROM.corruptCommit = false;
  command(serial, "SAVE\n");
  assert(serial.output.find("\"ok\":true") != std::string::npos);
  config = custom;
  assert(loadDeviceConfig(config) && equal(config, DEFAULT_CONFIG)); // RESET + SAVE equivalent.
  const int beforeCorruption = EEPROM.commits;
  EEPROM.flash[8] ^= 1; // RAM cache still matches config: SAVE must compare actual Flash.
  command(serial, "SAVE\n");
  assert(EEPROM.commits == beforeCorruption + 1);
  assert(loadDeviceConfig(config) && equal(config, DEFAULT_CONFIG));
  std::cout << "PASS: record validation/fallback, load, runtime-only SET/RESET, SAVE validation/readback/failure/wear\n";
}
