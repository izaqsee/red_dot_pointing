#include "tusb.h"
#include "hid.h"

bool redpoint_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!tud_hid_n_ready(HID_MOUSE)) return false;
  const int8_t report[] = {(int8_t)buttons, x, y, wheel, pan};
  return tud_hid_n_report(HID_MOUSE, 0, report,
    tud_hid_n_get_protocol(HID_MOUSE) == HID_PROTOCOL_BOOT ? 3 : sizeof(report));
}
bool redpoint_keyboard_send(uint8_t modifiers, const uint8_t keys[6]) {
  return tud_hid_n_ready(HID_KEYBOARD) && tud_hid_n_keyboard_report(HID_KEYBOARD, 0, modifiers, keys);
}

// Only neutral reports: enumeration testing must not type or move the pointer.
void redpoint_hid_task(void) {
  static uint32_t last;
  uint32_t now = tusb_time_millis_api();
  if (now - last < 100) return;
  last = now;
  const uint8_t keys[6] = {0};
  redpoint_mouse_send(0, 0, 0, 0, 0);
  redpoint_keyboard_send(0, keys);
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen) {
  if (report_id || type != HID_REPORT_TYPE_INPUT || instance > HID_KEYBOARD) return 0;
  uint16_t len = instance == HID_KEYBOARD ? 8 :
    tud_hid_n_get_protocol(instance) == HID_PROTOCOL_BOOT ? 3 : 5;
  if (len > reqlen) len = reqlen;
  memset(buffer, 0, len);
  return len;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                          const uint8_t *buffer, uint16_t size) {
  // Keyboard LED output is accepted; physical indicators are outside Milestone A.
  (void)instance; (void)report_id; (void)type; (void)buffer; (void)size;
}
