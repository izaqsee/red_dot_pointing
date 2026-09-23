#include "tusb.h"
#include "hid.h"
#include "hid_state.h"

bool redpoint_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!tud_hid_n_ready(HID_MOUSE)) return false;
  const int8_t report[] = {(int8_t)buttons, x, y, wheel, pan};
  return tud_hid_n_report(HID_MOUSE, 0, report,
    tud_hid_n_get_protocol(HID_MOUSE) == HID_PROTOCOL_BOOT ? 3 : sizeof(report));
}
bool redpoint_keyboard_send(uint8_t modifiers, const uint8_t keys[6]) {
  return tud_hid_n_ready(HID_KEYBOARD) && tud_hid_n_keyboard_report(HID_KEYBOARD, 0, modifiers, keys);
}

void redpoint_hid_task(void) {
  redpoint_hid_service(tud_ready());
}
// Reset/resume can both be serviced within one tud_task() call. Explicitly
// invalidate transmitted state even if main never observes tud_ready()==false.
void tud_mount_cb(void) { redpoint_hid_service(false); }
void tud_umount_cb(void) { redpoint_hid_service(false); }
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  redpoint_hid_service(false);
}
void tud_resume_cb(void) { redpoint_hid_service(false); }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen) {
  if (report_id || type != HID_REPORT_TYPE_INPUT || instance > HID_KEYBOARD) return 0;
  uint16_t len = instance == HID_KEYBOARD ? 8 :
    tud_hid_n_get_protocol(instance) == HID_PROTOCOL_BOOT ? 3 : 5;
  if (len > reqlen) len = reqlen;
  uint8_t report[8];
  if (instance == HID_MOUSE) redpoint_hid_current_mouse(report);
  else redpoint_hid_current_keyboard(report);
  memcpy(buffer, report, len);
  return len;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                          const uint8_t *buffer, uint16_t size) {
  // Keyboard LED output is accepted; physical indicators are outside Milestone A.
  (void)instance; (void)report_id; (void)type; (void)buffer; (void)size;
}
