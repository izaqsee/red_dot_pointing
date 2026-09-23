#include "test_platform.h"
#include "platform_io.h"
#include "hid.h"
#include "hid_state.h"
#include "input_runtime.h"
#include "config_platform.h"
#include <cstring>
#include <cassert>
uint32_t now;
bool pressed[3], flashAvailable = true, failCommit, corruptCommit, ledCanSend = true, hidCanSend = true;
uint8_t flashImage[4096];
unsigned writes, resyncs, ledShows;
uint32_t ledColor;
uint32_t ledColorAtCommit;
std::vector<std::array<int, 5>> mouseReports;
std::vector<std::array<uint8_t, 8>> keyReports;
extern "C" uint32_t redpoint_platform_millis(void) { return now; }
extern "C" uint32_t redpoint_irq_save(void) { return 0; }
extern "C" void redpoint_irq_restore(uint32_t) {}
extern "C" bool redpoint_button_pressed(unsigned i) { return pressed[i]; }
extern "C" void redpoint_input_irq_clear(void) { ++resyncs; }
extern "C" bool redpoint_led_init(void) { return true; }
extern "C" bool redpoint_led_ready(void) { return ledCanSend; }
extern "C" void redpoint_led_write(uint8_t r, uint8_t g, uint8_t b) {
  assert(ledCanSend); ledColor = (r << 16) | (g << 8) | b; ++ledShows;
}
extern "C" bool redpoint_storage_init(void) { return flashAvailable; }
extern "C" bool redpoint_storage_read(uint8_t *record, size_t size) {
  if (!flashAvailable) return false;
  assert(size <= sizeof(flashImage)); memcpy(record, flashImage, size); return true;
}
extern "C" bool redpoint_storage_commit(const uint8_t *record, size_t size) {
  ledColorAtCommit = ledColor;
  ++writes; memset(flashImage, 255, sizeof(flashImage));
  if (failCommit) { memcpy(flashImage, record, size/2); return false; }
  memcpy(flashImage, record, size);
  if (corruptCommit) flashImage[12] ^= 1;
  return true;
}
extern "C" bool redpoint_mouse_send(uint8_t b, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!hidCanSend) return false;
  mouseReports.push_back({b, x, y, wheel, pan}); return true;
}
extern "C" bool redpoint_keyboard_send(uint8_t mods, const uint8_t keys[6]) {
  if (!hidCanSend) return false;
  std::array<uint8_t, 8> r{}; r[0] = mods; memcpy(r.data()+2, keys, 6); keyReports.push_back(r); return true;
}
void testDrain(void) { for (int i = 0; i < 300; ++i) redpoint_hid_service(true); }
void testReset(void) {
  memset(pressed, 0, sizeof(pressed)); memset(flashImage, 255, sizeof(flashImage));
  flashAvailable = ledCanSend = hidCanSend = true; failCommit = corruptCommit = false;
  now += 100;
  redpoint_hid_service(false);
  redpoint_config_init(); redpoint_config_end_boot(); redpoint_input_init();
  testDrain(); mouseReports.clear(); keyReports.clear(); writes = resyncs = 0;
}
void testFrame(uint8_t value, bool parityOkay, bool stopOkay) {
  redpoint_input_edge(false, now);
  bool parity = true;
  for (unsigned i = 0; i < 8; ++i) {
    bool bit = (value >> i) & 1; parity ^= bit; redpoint_input_edge(bit, now);
  }
  redpoint_input_edge(parityOkay ? parity : !parity, now);
  redpoint_input_edge(stopOkay, now);
}
void testPacket(int8_t x, int8_t y) {
  now += 4;
  testFrame(uint8_t(8 | (x < 0 ? 0x10 : 0) | (y < 0 ? 0x20 : 0)));
  testFrame((uint8_t)x); testFrame((uint8_t)y);
  redpoint_input_task(); testDrain();
}
