// TinyUSB byte-stream adapter. All command validation/mutation lives in config_command.cpp.
#include "config_platform.h"
#include "config.h"
#include "status_led.h"
#include "tusb.h"

namespace {
char line[CONFIG_LINE_CAPACITY];
size_t used = 0, sent = 0;
bool discard = false;
ConfigResponse reply{};
}
extern "C" void redpoint_config_cdc_task(void) {
  if (!tud_cdc_connected()) {
    used = sent = reply.length = 0;
    discard = false;
    tud_cdc_read_flush();
    tud_cdc_write_clear();
    return;
  }
  if (!statusLedReadyForCommand()) return;
  // Drain the complete response before executing another command, including when
  // the host is slow. Never truncate a 512-byte response to the 256-byte TX FIFO.
  if (sent < reply.length) {
    sent += tud_cdc_write(reply.text + sent, (uint32_t)(reply.length - sent));
    tud_cdc_write_flush();
    return;
  }
  for (unsigned budget = 0; budget < 32 && tud_cdc_available(); ++budget) {
    const int byte = tud_cdc_read_char();
    if (byte < 0) break;
    if (byte == '\r' || byte == '\n') {
      if (!used && !discard) continue;
      const char invalid = 0;
      executeConfigCommand(discard ? &invalid : line, discard ? 1 : used, reply);
      used = sent = 0; discard = false;
      break;
    }
    if (!discard) {
      if ((byte < 32 && byte != '\t') || byte > 126 || used == sizeof(line) - 1)
        discard = true;
      else line[used++] = (char)byte;
    }
  }
}
