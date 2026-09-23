/* Based on TinyUSB net_lwip_webserver. See LICENSE.reference. */
#include "bsp/board_api.h"
#include "class/net/net_device.h"
#include "tusb.h"
#include "hid.h"

enum { ITF_NCM_CONTROL, ITF_NCM_DATA, ITF_CDC_CONTROL, ITF_CDC_DATA,
       ITF_MOUSE, ITF_KEYBOARD, ITF_COUNT };
enum { STR_LANG, STR_MAKER, STR_PRODUCT, STR_SERIAL, STR_NET, STR_MAC,
       STR_CDC, STR_MOUSE, STR_KEYBOARD };

const tusb_desc_device_t desc_device = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0201, .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0xcafe, .idProduct = 0x4019,
  .bcdDevice = 0x0100, .iManufacturer = STR_MAKER, .iProduct = STR_PRODUCT,
  .iSerialNumber = STR_SERIAL, .bNumConfigurations = 1
};
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }

const uint8_t desc_mouse_report[] = { TUD_HID_REPORT_DESC_MOUSE() };
const uint8_t desc_keyboard_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  return instance == HID_MOUSE ? desc_mouse_report :
         instance == HID_KEYBOARD ? desc_keyboard_report : NULL;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN + TUD_CDC_DESC_LEN + 2*TUD_HID_DESC_LEN)
const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, 0, 100),
  TUD_CDC_NCM_DESCRIPTOR(ITF_NCM_CONTROL, STR_NET, STR_MAC, 0x81, 64, 0x02, 0x82, 64,
    CFG_TUD_NET_MTU, 50, (NCM_NETWORK_CAPS_ETH_FILTER | NCM_NETWORK_CAPS_NTB_INPUT_SIZE)),
  TUD_CDC_DESCRIPTOR(ITF_CDC_CONTROL, STR_CDC, 0x83, 8, 0x04, 0x84, 64),
  TUD_HID_DESCRIPTOR(ITF_MOUSE, STR_MOUSE, HID_ITF_PROTOCOL_MOUSE,
    sizeof(desc_mouse_report), 0x85, 8, 10),
  TUD_HID_DESCRIPTOR(ITF_KEYBOARD, STR_KEYBOARD, HID_ITF_PROTOCOL_KEYBOARD,
    sizeof(desc_keyboard_report), 0x86, 8, 10)
};
TU_VERIFY_STATIC(CONFIG_TOTAL_LEN == 210, "Review configuration layout when changing classes");
TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "Configuration length mismatch");
TU_VERIFY_STATIC(CFG_TUD_CDC == 1 && CFG_TUD_HID == 2 && CFG_TUD_NCM == 1 && CFG_TUD_ECM_RNDIS == 0,
                 "Class counts must match descriptors");
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? desc_configuration : NULL;
}

// WINNCM is scoped to the NCM IAD's first interface, never the CDC ACM function.
#include "windows_ncm.h"

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t out[33];
  static const char *const strings[] = {
    NULL, "RedPoint", "RedPoint RevA Milestone A", NULL, "RedPoint NCM", NULL,
    "RedPoint Serial", "RedPoint Mouse", "RedPoint Keyboard"
  };
  size_t n = 0;
  if (index == STR_LANG) { out[1] = 0x0409; n = 1; }
  else if (index == STR_SERIAL) n = board_usb_get_serial(out + 1, 32);
  else if (index == STR_MAC) {
    for (size_t i = 0; i < 6; ++i) {
      out[++n] = "0123456789ABCDEF"[tud_network_mac_address[i] >> 4];
      out[++n] = "0123456789ABCDEF"[tud_network_mac_address[i] & 15];
    }
  } else {
    if (index >= TU_ARRAY_SIZE(strings) || !strings[index]) return NULL;
    n = strlen(strings[index]);
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; ++i) out[i+1] = (uint8_t)strings[index][i];
  }
  out[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2*n + 2));
  return out;
}
