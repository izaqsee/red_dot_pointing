// Behavior port of firmware/redpoint/redpoint.ino. No GPIO/USB/Arduino dependency.
#include "input_runtime.h"
#include "platform_io.h"
#include "hid_state.h"
#include "config.h"

namespace {
constexpr uint32_t PACKET_GAP_MS = 4, DEBOUNCE_MS = 5;
constexpr uint8_t FIFO_SIZE = 128;
struct Ps2Byte { uint8_t data; bool packetStart; };
volatile Ps2Byte fifo[FIFO_SIZE];
volatile uint8_t head, tail, bitIndex, dataByte, parityBit;
volatile uint32_t lastByteMs, dropped;
volatile bool haveLastByte;
uint8_t packet[3], packetIndex;
bool synced;
bool scrollMode;
float pointerAccX, pointerAccY, scrollAccX, scrollAccY;
struct Button { ButtonAction active; bool raw, stable; uint32_t changedAt; };
Button buttons[3];
const ButtonAction &action(unsigned i) {
  return i == 0 ? config.leftAction : i == 1 ? config.middleAction : config.rightAction;
}
void push(uint8_t value, bool start) {
  const uint8_t next = (head + 1) % FIFO_SIZE;
  if (next == tail) { ++dropped; return; }
  fifo[head].data = value; fifo[head].packetStart = start; head = next;
}
bool pop(Ps2Byte &value) {
  uint32_t irq = redpoint_irq_save();
  bool available = tail != head;
  if (available) {
    value.data = fifo[tail].data; value.packetStart = fifo[tail].packetStart;
    tail = (tail + 1) % FIFO_SIZE;
  }
  redpoint_irq_restore(irq);
  return available;
}
void updateButtons() {
  uint32_t now = redpoint_platform_millis();
  for (unsigned i = 0; i < 3; ++i) {
    auto &b = buttons[i]; const bool pressed = redpoint_button_pressed(i);
    if (pressed != b.raw) { b.raw = pressed; b.changedAt = now; }
    if (pressed != b.stable && uint32_t(now - b.changedAt) >= DEBOUNCE_MS) {
      b.stable = pressed;
      if (pressed) pressButtonAction(b.active, action(i)); else releaseButtonAction(b.active);
    }
  }
}
int16_t clamp(int16_t v) { return v > 127 ? 127 : v < -127 ? -127 : v; }
void move(uint8_t xb, uint8_t yb) {
  int16_t x = clamp((int8_t)yb), y = clamp((int8_t)xb);
  if (config.invertX) x = -x;
  if (config.invertY) y = -y;
  int16_t outX, outY;
  if (scrollMode) {
    pointerAccX = pointerAccY = 0;
    scrollAccX += x * config.middleSensitivity; scrollAccY += y * config.middleSensitivity;
    outX = (int16_t)scrollAccX; outY = (int16_t)scrollAccY;
    // Match pointer saturation: retain fractions, discard excess whole ticks.
    scrollAccX -= outX; scrollAccY -= outY;
    redpoint_hid_scroll((int8_t)clamp(outY), (int8_t)clamp(outX));
  } else {
    pointerAccX += x * config.pointerSensitivity; pointerAccY += y * config.pointerSensitivity;
    outX = (int16_t)pointerAccX; outY = (int16_t)pointerAccY;
    pointerAccX -= outX; pointerAccY -= outY;
    scrollAccX = scrollAccY = 0;
    redpoint_hid_motion((int8_t)clamp(outX), (int8_t)clamp(outY));
  }
}
}
extern "C" void redpoint_input_edge(bool bit, uint32_t now) {
  if (!bitIndex) { if (!bit) { dataByte = 0; bitIndex = 1; } return; }
  if (bitIndex <= 8) { if (bit) dataByte |= 1u << (bitIndex - 1); ++bitIndex; return; }
  if (bitIndex == 9) { parityBit = bit; ++bitIndex; return; }
  bitIndex = 0;
  if (!bit) return;
  uint8_t ones = parityBit;
  for (int i = 0; i < 8; ++i) ones += (dataByte >> i) & 1;
  if (!(ones & 1)) return;
  const bool start = !haveLastByte || uint32_t(now - lastByteMs) >= PACKET_GAP_MS;
  lastByteMs = now; haveLastByte = true; push(dataByte, start);
}
extern "C" void redpoint_input_init(void) {
  head = tail = bitIndex = dataByte = parityBit = packetIndex = 0;
  haveLastByte = synced = false; dropped = 0;
  redpoint_input_config_changed();
  for (unsigned i = 0; i < 3; ++i) {
    releaseButtonAction(buttons[i].active);
    bool pressed = redpoint_button_pressed(i);
    buttons[i] = {DISABLED_ACTION, pressed, pressed, redpoint_platform_millis()};
    // Like Arduino's startup latch: USB output waits for enumeration in the backend.
    if (pressed) pressButtonAction(buttons[i].active, action(i));
  }
  scrollMode = mouseActionHeld(MouseButtonCode::Middle);
}
extern "C" void redpoint_input_config_changed(void) {
  pointerAccX = pointerAccY = scrollAccX = scrollAccY = 0;
}
extern "C" void redpoint_input_post_flash(void) {
  uint32_t irq = redpoint_irq_save();
  tail = head; bitIndex = dataByte = parityBit = 0;
  lastByteMs = redpoint_platform_millis(); haveLastByte = true;
  redpoint_input_irq_clear();
  redpoint_irq_restore(irq);
  packetIndex = 0; synced = false;
}
extern "C" uint32_t redpoint_input_dropped(void) { return dropped; }
extern "C" void redpoint_input_task(void) {
  updateButtons();
  const bool middle = mouseActionHeld(MouseButtonCode::Middle);
  if (middle != scrollMode) {
    // Reset even if no packet arrived during a short hold/release interval.
    redpoint_input_config_changed();
    scrollMode = middle;
  }
  Ps2Byte b;
  // Bound a busy producer; network/CDC must regain main-loop ownership.
  for (unsigned budget = 0; budget < FIFO_SIZE && pop(b); ++budget) {
    if (b.packetStart) { packetIndex = 0; synced = true; }
    if (!synced) continue;
    packet[packetIndex++] = b.data;
    if (packetIndex == 3) {
      bool sane = bool(packet[0] & 0x10) == bool(packet[1] & 0x80) &&
                  bool(packet[0] & 0x20) == bool(packet[2] & 0x80);
      if (sane) move(packet[1], packet[2]); else synced = false;
      packetIndex = 0;
    }
  }
}
