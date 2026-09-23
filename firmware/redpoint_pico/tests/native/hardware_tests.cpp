#include "test_platform.h"
#include "input_runtime.h"
#include "hid_state.h"
#include "config_platform.h"
#include "config.h"
#include "config_record.h"
#include "config_storage.h"
#include "status_led.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
static void command(const char *text) {
  ConfigResponse r; executeConfigCommand(text, strlen(text), r); redpoint_config_apply();
}
static void button(unsigned index, bool value) {
  pressed[index] = value; redpoint_input_task(); now += 5; redpoint_input_task(); testDrain();
}
static void repairCRC(uint8_t *data, unsigned length) {
  uint32_t crc = 0xffffffff;
  for (unsigned i = 0; i < length-4; ++i) {
    crc ^= data[i]; for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  crc ^= 0xffffffff;
  for (int i = 0; i < 4; ++i) data[length-4+i] = uint8_t(crc >> (8*i));
}
void runHardwareTests(void) {
  testReset(); testPacket(3, -2);
  assert((mouseReports.back() == std::array<int,5>{0,-2,3,0,0}));
  command("SET pointerInvertX 1"); command("SET pointerInvertY 1"); testPacket(3, -2);
  assert((mouseReports.back() == std::array<int,5>{0,2,-3,0,0}));
  testReset(); command("SET pointerSensitivity 0.5");
  testPacket(1, 1); assert(mouseReports.back()[1] == 0);
  testPacket(1, 1); assert(mouseReports.back()[1] == 1);
  testPacket(-1, -1); assert(mouseReports.back()[1] == 0);
  testPacket(-1, -1); assert(mouseReports.back()[1] == -1);
  testPacket(1, 1); command("SET pointerSensitivity 0.5"); testPacket(1, 1);
  assert(mouseReports.back()[1] == 0); // main-loop change resets fractions
  command("SET pointerSensitivity 10"); testPacket(127, -128);
  assert((mouseReports.back() == std::array<int,5>{0,-127,127,0,0}));
  command("SET pointerSensitivity 0.5"); testPacket(1, 1); command("RESET");
  command("SET pointerSensitivity 0.5"); testPacket(1,1); assert(mouseReports.back()[1] == 0);

  testReset(); now += 4; testFrame(8, false); testFrame(1); testFrame(1);
  redpoint_input_task(); testDrain(); assert(mouseReports.empty());
  redpoint_input_post_flash(); now += 4; testFrame(8, true, false);
  redpoint_input_task(); testDrain(); assert(mouseReports.empty());
  redpoint_input_post_flash(); now += 4; testFrame(8); testFrame(255); testFrame(1);
  redpoint_input_task(); testDrain(); assert(mouseReports.empty()); // sign mismatch
  testFrame(8); testFrame(1); testFrame(1); redpoint_input_task(); testDrain();
  assert(mouseReports.empty()); // wait for new packet gap
  testPacket(2,3); assert(mouseReports.back()[1] == 3);
  redpoint_input_post_flash();
  for (int i = 0; i < 140; ++i) testFrame(0);
  assert(redpoint_input_dropped() > 0);
  redpoint_input_post_flash(); mouseReports.clear(); testPacket(1,2);
  assert(mouseReports.back()[1] == 2);
  now = 0xfffffffe; testPacket(1,1); testPacket(1,1); // unsigned timing rollover
  assert(mouseReports.back()[1] == 1);
  std::cout << "PASS: production PS/2 frame/FIFO/parity/stop/gap/sanity, axes/invert/sensitivity/fractions/reset/saturation\n";

  testReset(); pressed[0] = true; redpoint_input_task(); now += 4; redpoint_input_task(); testDrain();
  assert(!mouseActionHeld(MouseButtonCode::Left));
  now += 1; redpoint_input_task(); testDrain(); assert(mouseActionHeld(MouseButtonCode::Left));
  command("SET leftAction key:03:17"); button(0,false);
  assert(!mouseActionHeld(MouseButtonCode::Left) && mouseReports.back()[0] == 0);
  button(0,true); assert(keyReports.back()[0] == 3 && keyReports.back()[2] == 0x17);
  command("SET leftAction disabled"); button(0,false);
  for (auto byte : keyReports.back()) assert(byte == 0);
  auto count = keyReports.size(); button(0,true); button(0,false); assert(keyReports.size() == count);
  command("SET leftAction mouse:middle"); command("SET rightAction mouse:middle");
  button(0,true); button(2,true); button(0,false);
  assert(mouseActionHeld(MouseButtonCode::Middle));
  command("SET middleSensitivity 0.5"); command("SET pointerSensitivity 2");
  testPacket(2,2); assert((mouseReports.back() == std::array<int,5>{0,0,0,-1,1})); // logical Middle scroll
  testPacket(1,1); assert(mouseReports.back()[4] == 0);
  command("SET middleSensitivity 0.5"); testPacket(1,1); assert(mouseReports.back()[4] == 0);
  button(2,false); testPacket(2,2); assert(mouseReports.back()[1] == 4);
  command("SET middleAction disabled"); button(1,true); testPacket(2,2);
  assert(mouseReports.back()[1] == 4); button(1,false);
  command("SET leftAction key:03:17"); command("SET rightAction key:03:17");
  button(0,true); button(2,true); button(0,false);
  assert(keyReports.back()[0] == 3 && keyReports.back()[2] == 0x17);
  button(2,false); for (auto byte : keyReports.back()) assert(byte == 0);
  command("SET rightAction key:01:04"); button(0,true); button(2,true); button(0,false);
  assert(keyReports.back()[0] == 1); bool found = false;
  for (unsigned i=2;i<8;++i) found |= keyReports.back()[i] == 4;
  assert(found); button(2,false);
  hidCanSend = false; button(0,true); button(0,false); assert(keyReports.back()[0] == 0);
  hidCanSend = true; testDrain(); assert(keyReports.back()[0] == 0);
  button(0,true); redpoint_hid_service(false); button(0,false);
  // button helper drained/reconnected: final state still must be released.
  testDrain(); for (auto byte : keyReports.back()) assert(byte == 0);
  pressed[0] = true; redpoint_input_init(); testDrain(); assert(keyReports.back()[0] == 3);
  redpoint_hid_service(false); pressed[0] = false; redpoint_input_task(); now += 5;
  redpoint_input_task(); testDrain(); for (auto byte : keyReports.back()) assert(byte == 0);
  hidCanSend = false;
  for (int i = 0; i < 150; ++i) { button(0,true); button(0,false); }
  hidCanSend = true; testDrain(); for (auto byte : keyReports.back()) assert(byte == 0);
  command("SET leftAction mouse:left"); hidCanSend = false;
  for (int i = 0; i < 150; ++i) { button(0,true); button(0,false); }
  hidCanSend = true; testDrain(); assert(mouseReports.back()[0] == 0);
  std::cout << "PASS: debounce/startup latch/configured actions/shared refcounts/overlapping modifiers/busy/suspend release\n";

  testReset(); command("SET pointerSensitivity 0.5"); testPacket(1,1);
  now += 4; testFrame(8); testFrame(1); // partial packet before SAVE
  command("SAVE"); assert(writes == 1 && resyncs == 1 && !configUnsaved());
  assert(ledColorAtCommit == 0xff00ff);
  auto saved = config; command("SAVE"); assert(writes == 1 && resyncs == 1);
  testFrame(1); redpoint_input_task(); testDrain(); // stale tail cannot finish a packet
  auto reports = mouseReports.size();
  testPacket(1,1); assert(mouseReports.size() == reports+1 && mouseReports.back()[1] == 1); // fraction preserved by Flash resync
  config = DEFAULT_CONFIG; assert(loadDeviceConfig(config) && equalDeviceConfig(config, saved));
  command("SET pointerInvertX 1"); failCommit = true; command("SAVE");
  assert(resyncs == 2 && configUnsaved());
  failCommit = false; corruptCommit = true; command("SAVE"); assert(resyncs == 3 && configUnsaved());
  corruptCommit = false; command("SAVE"); assert(!configUnsaved());
  DeviceConfig restored; flashImage[12] ^= 1; assert(!loadDeviceConfig(restored));
  assert(equalDeviceConfig(restored, DEFAULT_CONFIG));
  // v2 migration keeps actions/pointer, initializes independent wheel inversions.
  auto old = saved; old.pointerInvertX = true; old.wheelInvertX = true;
  old.wheelSensitivityX = 0.25f; old.wheelSensitivityY = 1.5f;
  old.leftAction = {ActionType::KeyboardShortcut, 0x17, 0x03};
  encodeConfigRecord(old, flashImage); flashImage[4] = 2; flashImage[6] = 36;
  repairCRC(flashImage, 36); unsigned beforeV2Writes = writes;
  assert(loadDeviceConfig(restored));
  assert(restored.pointerInvertX && !restored.wheelInvertX && !restored.wheelInvertY);
  assert(restored.wheelSensitivityX == 0.25f && restored.wheelSensitivityY == 0.25f);
  assert(restored.leftAction.code == 0x17 && writes == beforeV2Writes && flashImage[4] == 2);
  assert(saveDeviceConfig(restored) == ConfigSaveResult::Saved && flashImage[4] == 3);
  assert(takeConfigFlashWrite());
  DeviceConfig reboot; assert(loadDeviceConfig(reboot) && equalDeviceConfig(restored, reboot));
  // v1 fixture: same codec/CRC, no boot write; next explicit SAVE upgrades to v3.
  encodeConfigRecord(saved, flashImage); flashImage[4] = 1; flashImage[6] = 24;
  repairCRC(flashImage, 24); unsigned beforeWrites = writes;
  assert(loadDeviceConfig(restored)); assert(restored.pointerSensitivity == saved.pointerSensitivity);
  assert(restored.leftAction.code == LEFT_ACTION.code && writes == beforeWrites);
  assert(saveDeviceConfig(restored) == ConfigSaveResult::Saved && flashImage[4] == 3);
  assert(takeConfigFlashWrite());
  restored.pointerSensitivity = std::numeric_limits<float>::quiet_NaN();
  assert(saveDeviceConfig(restored) == ConfigSaveResult::InvalidConfig);
  assert(!takeConfigFlashWrite());
  flashAvailable = false; assert(!loadDeviceConfig(restored));
  assert(saveDeviceConfig(DEFAULT_CONFIG) == ConfigSaveResult::StorageError);
  assert(!takeConfigFlashWrite());
  std::cout << "PASS: v3/CRC/v1+v2 migration/explicit SAVE/readback/reboot/no-op/failure resync and baseline semantics\n";

  testReset(); statusLedBegin(); assert(ledColor == 0xffffff);
  statusLedEndBoot(); assert(ledColor == 0x0000ff);
  statusLedActivity(); statusLedUpdate(); assert(ledColor == 0x00ff00);
  now += 6000; statusLedUpdate(); assert(ledColor == 0x0000ff);
  statusLedSetUnsaved(true); statusLedUpdate(); assert(ledColor == 0xffff00);
  statusLedSetSaving(true); assert(ledColor == 0xff00ff);
  statusLedSetSaving(false); now += 149; statusLedUpdate(); assert(ledColor == 0xff00ff);
  now += 1; statusLedUpdate(); assert(ledColor == 0xffff00);
  statusLedSignalError(); statusLedUpdate(); assert(ledColor == 0xff0000);
  now += 2000; statusLedUpdate(); assert(ledColor == 0xffff00);
  ledCanSend = false; statusLedSetUnsaved(false); statusLedUpdate();
  assert(!statusLedReadyForCommand() && ledColor == 0xffff00);
  ledCanSend = true; statusLedUpdate(); assert(ledColor == 0x0000ff);
  unsigned shows = ledShows; statusLedUpdate(); assert(ledShows == shows);
  std::cout << "PASS: LED boot/colors/priority/activity/saving/error/latch/nonblocking unchanged-only semantics\n";
  testReset();
}
