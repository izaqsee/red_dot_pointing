#include "config_serial.h"
#include "config.h"
#include "status_led.h"
#include <Arduino.h>
namespace {
char line[CONFIG_LINE_CAPACITY];
size_t lineLength = 0;
bool discardLine = false;
constexpr size_t RX_BYTES_PER_LOOP = 32;
}
bool pollConfigSerial(Stream &serial) {
  // A prior color's latch must finish before a possible SAVE can submit purple.
  // Yield to HID/PS2 instead of waiting inside NeoPixel::show().
  if (!statusLedReadyForCommand()) return false;
  bool changed = false;
  for (size_t count = 0; count < RX_BYTES_PER_LOOP && serial.available(); ++count) {
    const int incoming = serial.read();
    if (incoming < 0) break;
    if (incoming == '\r' || incoming == '\n') {
      const bool hadLine = discardLine || lineLength != 0;
      if (discardLine) {
        ConfigResponse response;
        const char invalid = 0;
        executeConfigCommand(&invalid, 1, response);
        serial.print(response.text);
      }
      else if (lineLength) {
        ConfigResponse response;
        if (executeConfigCommand(line, lineLength, response)) changed = true;
        serial.print(response.text);
      }
      lineLength = 0;
      discardLine = false;
      // At most one non-empty line per loop; return to input processing.
      if (hadLine) break;
    } else if (!discardLine) {
      if ((incoming < 32 && incoming != '\t') || incoming > 126 ||
          lineLength >= CONFIG_LINE_CAPACITY - 1) {
        discardLine = true;
      } else {
        line[lineLength++] = static_cast<char>(incoming);
      }
    }
  }
  return changed;
}
