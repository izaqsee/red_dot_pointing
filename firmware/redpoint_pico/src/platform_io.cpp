#include "platform_io.h"
#include "input_runtime.h"
#include "status_led.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
namespace {
PIO ledPio = pio0;
int ledSm = -1;
uint32_t ledAt;
bool ledSent;
const uint pins[] = {REDPOINT_LEFT, REDPOINT_MIDDLE, REDPOINT_RIGHT};
void gpioEdge(uint gpio, uint32_t events) {
  if (gpio == REDPOINT_CLK && (events & GPIO_IRQ_EDGE_FALL))
    redpoint_input_edge(gpio_get(REDPOINT_DATA), to_ms_since_boot(get_absolute_time()));
}
}
extern "C" uint32_t redpoint_irq_save(void) { return save_and_disable_interrupts(); }
extern "C" void redpoint_irq_restore(uint32_t state) { restore_interrupts(state); }
extern "C" bool redpoint_button_pressed(unsigned index) { return index < 3 && !gpio_get(pins[index]); }
extern "C" void redpoint_hardware_init(void) {
  for (uint pin : pins) { gpio_init(pin); gpio_set_dir(pin, GPIO_IN); gpio_pull_up(pin); }
  gpio_init(REDPOINT_CLK); gpio_set_dir(REDPOINT_CLK, GPIO_IN);
  gpio_init(REDPOINT_DATA); gpio_set_dir(REDPOINT_DATA, GPIO_IN);
}
extern "C" void redpoint_input_irq_enable(void) {
  gpio_set_irq_enabled_with_callback(REDPOINT_CLK, GPIO_IRQ_EDGE_FALL, true, gpioEdge);
}
extern "C" void redpoint_input_irq_clear(void) { gpio_acknowledge_irq(REDPOINT_CLK, GPIO_IRQ_EDGE_FALL); }
extern "C" bool redpoint_led_init(void) {
  if (ledSm >= 0) return true;
  if (!pio_can_add_program(ledPio, &ws2812_program)) return false;
  ledSm = pio_claim_unused_sm(ledPio, false);
  if (ledSm < 0) return false;
  uint offset = pio_add_program(ledPio, &ws2812_program);
  ws2812_program_init(ledPio, (uint)ledSm, offset, REDPOINT_LED, 800000, false);
  ledSent = false;
  return true;
}
extern "C" bool redpoint_led_ready(void) {
  // 24 bits at 800 kHz = 30 us, plus NeoPixel's conservative 300 us reset latch.
  return ledSm >= 0 && (!ledSent || uint32_t(time_us_32() - ledAt) >= 330) &&
    !pio_sm_is_tx_fifo_full(ledPio, (uint)ledSm);
}
extern "C" void redpoint_led_write(uint8_t r, uint8_t g, uint8_t b) {
  if (!redpoint_led_ready()) return;
  // Same brightness scaling as NeoPixel.setBrightness(8): (channel * 9) >> 8.
  r = (uint8_t)((r * (STATUS_LED_BRIGHTNESS + 1)) >> 8);
  g = (uint8_t)((g * (STATUS_LED_BRIGHTNESS + 1)) >> 8);
  b = (uint8_t)((b * (STATUS_LED_BRIGHTNESS + 1)) >> 8);
  pio_sm_put(ledPio, (uint)ledSm, ((uint32_t(g) << 16) | (uint32_t(r) << 8) | b) << 8);
  ledAt = time_us_32(); ledSent = true;
}
