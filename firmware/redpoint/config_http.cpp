#include "config_http.h"
#include "status_led.h"

int executeConfigHttp(const char *body, size_t length, ConfigResponse &response) {
  response.length = 0; response.text[0] = 0;
  if (length > CONFIG_LINE_CAPACITY) return 413;
  // Accept the protocol's leading resync newline and trailing CR/LF, but never
  // a batch: validate the entire body before allowing any mutation.
  while (length && (*body == '\r' || *body == '\n')) { ++body; --length; }
  while (length && (body[length - 1] == '\r' || body[length - 1] == '\n')) --length;
  if (!length || length >= CONFIG_LINE_CAPACITY) return 400;
  bool nonspace = false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = body[i];
    if ((c < 32 && c != '\t') || c > 126) return 400;
    if (c != ' ' && c != '\t') nonspace = true;
  }
  if (!nonspace) return 400;
  if (!statusLedReadyForCommand()) return 503;
  executeConfigCommand(body, length, response);
  return 200;
}
