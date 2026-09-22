#include "config.h"
#include "status_led.h"
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
uint32_t testMillis = 0, ledColor = 0;
int ledShows = 0;
bool ledCanShow = true;
void runStatusTests();

void runActionTests();
bool equalAction(const ButtonAction &a, const ButtonAction &b) {
  return a.type == b.type && a.code == b.code && a.modifiers == b.modifiers;
}
bool equal(const DeviceConfig &a, const DeviceConfig &b) {
  return a.pointerSensitivity == b.pointerSensitivity && a.middleSensitivity == b.middleSensitivity &&
    a.invertX == b.invertX && a.invertY == b.invertY &&
    equalAction(a.leftAction,b.leftAction) && equalAction(a.middleAction,b.middleAction) && equalAction(a.rightAction,b.rightAction);
}

// Independent CRC fixture helper to test semantic rejection despite a valid CRC.
void repairCRC(uint8_t *record, size_t length = CONFIG_RECORD_SIZE) {
  uint32_t crc = 0xffffffff;
  for (int i = 0; i < int(length - 4); ++i) {
    crc ^= record[i];
    for (int j = 0; j < 8; ++j) crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
  }
  crc ^= 0xffffffff;
  for (int i = 0; i < 4; ++i) record[length - 4 + i] = (crc >> (8 * i)) & 255;
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
  runActionTests();
  const DeviceConfig custom = {1.25f, 0.35f, true, false, LEFT_ACTION, MIDDLE_ACTION, RIGHT_ACTION};
  uint8_t record[CONFIG_RECORD_SIZE];
  assert(encodeConfigRecord(custom, record));
  assert(memcmp(record, "RPNT", 4) == 0);
  assert(record[4] == 2 && record[5] == 0 && record[6] == 36 && record[7] == 0);
  // IEEE-754 1.25 = 0x3fa00000, explicitly little endian.
  assert(record[8] == 0 && record[9] == 0 && record[10] == 0xa0 && record[11] == 0x3f);
  DeviceConfig decoded;
  assert(decodeConfigRecord(record, decoded) && equal(decoded, custom));
  uint8_t damaged[CONFIG_RECORD_SIZE];
  DeviceConfig mixed = custom;
  mixed.leftAction = DISABLED_ACTION;
  mixed.middleAction = {ActionType::KeyboardShortcut, 0x17, 0x03};
  assert(encodeConfigRecord(mixed, damaged));
  assert(decodeConfigRecord(damaged, decoded) && equal(decoded, mixed));
  for (uint8_t badUsage : {0x00, 0x32, 0x66, 0x74, 0xe0}) {
    assert(encodeConfigRecord(mixed, damaged));
    damaged[25] = badUsage;
    repairCRC(damaged);
    expectFallback(damaged);
  }
  assert(encodeConfigRecord(mixed, damaged));
  damaged[26] = 0x10;
  repairCRC(damaged);
  expectFallback(damaged);
  assert(encodeConfigRecord(mixed, damaged));
  damaged[21] = 1; // Disabled must have zero payload.
  repairCRC(damaged);
  expectFallback(damaged);
  for (size_t offset = 0; offset < CONFIG_RECORD_SIZE; ++offset) {
    memcpy(damaged, record, sizeof(record));
    damaged[offset] ^= 1;
    expectFallback(damaged); // Includes every CRC byte and payload byte.
  }
  for (int offset : {0, 4, 6, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}) {
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
  configSetPersistentBaseline(config);
  assert(!configUnsaved());
  assert(EEPROM.commits == 0 && EEPROM.writes == 0); // Boot never repairs Flash.
  for (size_t i = 0; i < CONFIG_RECORD_SIZE; ++i) EEPROM.flash[i] = record[i];
  assert(loadDeviceConfig(config) && equal(config, custom));
  configSetPersistentBaseline(config);
  assert(!configUnsaved());

  Stream serial;
  assert(!command(serial, "SAVE\n"));
  assert(serial.output.find("\"command\":\"SAVE\"") != std::string::npos);
  assert(EEPROM.commits == 0); // Unchanged save doesn't wear Flash.
  assert(command(serial, "RESET\n"));
  assert(equal(config, DEFAULT_CONFIG));
  assert(EEPROM.commits == 0);
  assert(loadDeviceConfig(config) && equal(config, custom));
  configSetPersistentBaseline(config);
  assert(!configUnsaved()); // RESET without SAVE.

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
  configSetPersistentBaseline(config);
  assert(!configUnsaved());
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
  // Golden v1 fixture created independently with Python struct + zlib.
  const uint8_t legacy[24] = {0x52, 0x50, 0x4e, 0x54, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0xa0, 0x3f, 0x33, 0x33, 0xb3, 0x3e, 0x01, 0x00, 0x00, 0x00, 0xae, 0xd5, 0xb9, 0xdf};
  assert(decodeConfigRecord(legacy, decoded, sizeof(legacy)) && equal(decoded, custom));
  assert(!decodeConfigRecord(legacy, decoded, 23) && equal(decoded, DEFAULT_CONFIG));
  for (int offset = 0; offset < 24; ++offset) {
    uint8_t badLegacy[24]; memcpy(badLegacy, legacy, 24); badLegacy[offset] ^= 1;
    assert(!decodeConfigRecord(badLegacy, decoded, 24) && equal(decoded, DEFAULT_CONFIG));
  }
  uint8_t badLegacy[24]; memcpy(badLegacy, legacy, 24);
  badLegacy[16] = 2; repairCRC(badLegacy, 24);
  assert(!decodeConfigRecord(badLegacy, decoded, 24));
  memcpy(badLegacy, legacy, 24);
  badLegacy[11] = 0x7f; // Valid CRC, invalid sensitivity range.
  repairCRC(badLegacy, 24);
  assert(!decodeConfigRecord(badLegacy, decoded, 24) && equal(decoded, DEFAULT_CONFIG));
  const int beforeMigration = EEPROM.commits;
  EEPROM.flash.fill(0xff);
  for (size_t i = 0; i < sizeof(legacy); ++i) EEPROM.flash[i] = legacy[i];
  assert(loadDeviceConfig(config) && equal(config, custom));
  configSetPersistentBaseline(config);
  assert(!configUnsaved());
  assert(EEPROM.commits == beforeMigration && EEPROM.flash[4] == 1);
  command(serial, "GET\n");
  assert(serial.output.find("\"leftAction\":\"mouse:left\"") != std::string::npos);
  command(serial, "SAVE\n");
  assert(EEPROM.flash[4] == 2 && EEPROM.flash[6] == 36 && EEPROM.commits == beforeMigration + 1);
  command(serial, "SAVE\n"); assert(EEPROM.commits == beforeMigration + 1);
  assert(command(serial, "SET rightAction key:03:17\n"));
  assert(config.rightAction.type == ActionType::KeyboardShortcut && config.rightAction.code == 0x17);
  assert(serial.output.find("key:03:17") != std::string::npos);
  const DeviceConfig binding = config;
  assert(!command(serial, "SET rightAction key:10:17\n"));
  assert(equal(config, binding) && serial.output.find("INVALID_VALUE") != std::string::npos);
  command(serial, "SAVE\n");
  assert(loadDeviceConfig(config) && equal(config,binding));
  const int savedCount = EEPROM.commits;
  assert(command(serial, "RESET\n") && equal(config,DEFAULT_CONFIG));
  assert(EEPROM.commits == savedCount);
  assert(loadDeviceConfig(config) && equal(config,binding));
  config.rightAction = {ActionType::KeyboardShortcut,0,1};
  command(serial, "SAVE\n");
  assert(serial.output.find("INVALID_CONFIG") != std::string::npos && EEPROM.commits == savedCount);
  command(serial, "RESET\n"); command(serial, "SAVE\n");
  assert(loadDeviceConfig(config) && equal(config,DEFAULT_CONFIG));
  runStatusTests();
  std::cout << "PASS: record validation/fallback, load, runtime-only SET/RESET, SAVE validation/readback/failure/wear\n";
}

void runStatusTests() {
  statusLedBegin();
  assert(statusLedState() == StatusLedState::Boot && ledColor == 0xffffff);
  statusLedEndBoot();
  assert(statusLedState() == StatusLedState::Normal && ledColor == 0xff);
  int shows = ledShows;
  for (int i=0; i<100; ++i) statusLedUpdate();
  assert(ledShows == shows);
  statusLedActivity(); statusLedUpdate();
  assert(statusLedState() == StatusLedState::Connected && ledColor == 0xff00);
  statusLedSetUnsaved(true); statusLedUpdate();
  assert(statusLedState() == StatusLedState::Unsaved && ledColor == 0xffff00);
  statusLedSetSaving(true);
  assert(statusLedState() == StatusLedState::Saving && ledColor == 0xff00ff);
  statusLedSignalError(); statusLedUpdate();
  assert(statusLedState() == StatusLedState::Error && ledColor == 0xff0000);
  testMillis += 1999; assert(statusLedState() == StatusLedState::Error);
  statusLedSignalError(); testMillis += 1999; assert(statusLedState() == StatusLedState::Error);
  ++testMillis; assert(statusLedState() == StatusLedState::Saving);
  statusLedSetSaving(false); assert(statusLedState() == StatusLedState::Unsaved);
  testMillis += 6000; assert(statusLedState() == StatusLedState::Unsaved);
  statusLedSetUnsaved(false); assert(statusLedState() == StatusLedState::Normal);
  testMillis = 0xfffffff0u; statusLedActivity(); statusLedSignalError();
  testMillis += 2000; assert(statusLedState() == StatusLedState::Connected);
  testMillis += 3999; assert(statusLedState() == StatusLedState::Connected);
  ++testMillis; assert(statusLedState() == StatusLedState::Normal);
  statusLedUpdate(); shows = ledShows; ledCanShow = false;
  statusLedActivity(); statusLedUpdate(); assert(ledShows == shows);
  ledCanShow = true; statusLedUpdate(); assert(ledShows == shows+1);
  statusLedSetSaving(true); statusLedSetSaving(false);
  testMillis += 149; assert(statusLedState() == StatusLedState::Saving);
  ++testMillis; assert(statusLedState() == StatusLedState::Connected);
  config = DEFAULT_CONFIG; configSetPersistentBaseline(config);
  Stream serial;
  command(serial, "SET pointerSensitivity 2\n"); assert(configUnsaved());
  command(serial, "SET pointerSensitivity 1\n"); assert(!configUnsaved());
  for (const char *cmd : {"SET middleSensitivity 2\n", "SET invertX 1\n", "SET invertY 1\n",
       "SET leftAction disabled\n", "SET middleAction disabled\n", "SET rightAction key:01:04\n"}) {
    command(serial, cmd); assert(configUnsaved());
    command(serial, "RESET\n"); assert(!configUnsaved());
  }
  EEPROM.flash.fill(0xff); loadDeviceConfig(config); configSetPersistentBaseline(config);
  EEPROM.onCommit = []() { assert(statusLedState() == StatusLedState::Saving && ledColor == 0xff00ff); };
  command(serial, "SET invertX 1\n"); command(serial, "SAVE\n"); assert(!configUnsaved());
  EEPROM.onCommit = nullptr;
  command(serial, "RESET\n"); assert(configUnsaved());
  EEPROM.commitFails = true; command(serial, "SAVE\n"); assert(configUnsaved());
  command(serial, "SET invertX 1\n"); assert(!configUnsaved());
  EEPROM.commitFails = false;
  testMillis += 6000;
  for (const char *cmd : {"PING\n", "GET\n", "SET invertX 1\n", "RESET\n", "SAVE\n"}) {
    command(serial, cmd);
    testMillis += 5999;
    assert(statusLedState() == (configUnsaved() ? StatusLedState::Unsaved : StatusLedState::Connected));
    ++testMillis;
    assert(statusLedState() == (configUnsaved() ? StatusLedState::Unsaved : StatusLedState::Normal));
  }
  ledCanShow = false;
  serial.input = "SAVE\n"; serial.position = 0;
  assert(!pollConfigSerial(serial) && serial.position == 0);
  ledCanShow = true;
  command(serial, "PING extra\n"); assert(statusLedState() == StatusLedState::Error);
  testMillis += 2000; assert(statusLedState() == StatusLedState::Normal);
  // Restore fixtures for the pre-existing regression suite.
  EEPROM = MockEEPROM(); config = DEFAULT_CONFIG; configSetPersistentBaseline(config);
  takeConfigFlashWrite();
  std::cout << "PASS: status priority/timers/rollover, changed-only driver, semantic baseline and protocol activity\n";
}
