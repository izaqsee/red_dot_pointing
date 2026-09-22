#include "config.h"
#include "config_storage.h"
#include "status_led.h"

#include <Arduino.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t LINE_CAPACITY = 96;
constexpr size_t RX_BYTES_PER_LOOP = 32;
constexpr uint8_t JSON_DECIMALS = 6;

char line[LINE_CAPACITY];
size_t lineLength = 0;
bool discardLine = false;

void error(Stream &serial, const char *code) {
  statusLedSignalError();
  serial.print("@CONFIG {\"ok\":false,\"error\":\"");
  serial.print(code);
  serial.println("\"}");
}

void replyAction(Stream &serial, const char *key, const ButtonAction &action) {
  char text[ACTION_TEXT_CAPACITY];
  // All config entry points validate actions before assignment.
  if (!formatButtonAction(action, text, sizeof(text))) return;
  serial.print(",\"");
  serial.print(key);
  serial.print("\":\"");
  serial.print(text);
  serial.print("\"");
}

void reply(Stream &serial, const char *command) {
  serial.print("@CONFIG {\"ok\":true,\"command\":\"");
  serial.print(command);
  serial.print("\",\"config\":{\"pointerSensitivity\":");
  serial.print(config.pointerSensitivity, JSON_DECIMALS);
  serial.print(",\"middleSensitivity\":");
  serial.print(config.middleSensitivity, JSON_DECIMALS);
  serial.print(",\"invertX\":");
  serial.print(config.invertX ? "true" : "false");
  serial.print(",\"invertY\":");
  serial.print(config.invertY ? "true" : "false");
  replyAction(serial, "leftAction", config.leftAction);
  replyAction(serial, "middleAction", config.middleAction);
  replyAction(serial, "rightAction", config.rightAction);
  serial.println("}}");
}

bool parseSensitivity(const char *value, float &result) {
  // Decimal syntax only; reject NaN/Inf, hexadecimal and trailing garbage.
  bool digit = false;
  bool dot = false;
  for (const char *p = value; *p; ++p) {
    if (*p >= '0' && *p <= '9') digit = true;
    else if (*p == '.' && !dot) dot = true;
    else return false;
  }
  if (!digit) return false;
  char *end;
  const float parsed = strtof(value, &end);
  if (*end || !isfinite(parsed) ||
      parsed < MIN_SENSITIVITY || parsed > MAX_SENSITIVITY) return false;
  result = parsed;
  return true;
}

bool executeLine(Stream &serial) {
  char *state = nullptr;
  char *command = strtok_r(line, " \t", &state);
  if (!command) return false;
  char *key = strtok_r(nullptr, " \t", &state);
  if (strcmp(command, "SET") == 0) {
    char *value = strtok_r(nullptr, " \t", &state);
    if (!key || !value || strtok_r(nullptr, " \t", &state)) {
      error(serial, "INVALID_ARGUMENTS");
      return false;
    }
    if (strcmp(key, "pointerSensitivity") == 0 ||
        strcmp(key, "middleSensitivity") == 0) {
      float parsed;
      if (!parseSensitivity(value, parsed)) {
        error(serial, "INVALID_VALUE");
        return false;
      }
      if (strcmp(key, "pointerSensitivity") == 0) config.pointerSensitivity = parsed;
      else config.middleSensitivity = parsed;
    } else if (strcmp(key, "invertX") == 0 || strcmp(key, "invertY") == 0) {
      if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
        error(serial, "INVALID_VALUE");
        return false;
      }
      if (strcmp(key, "invertX") == 0) config.invertX = value[0] == '1';
      else config.invertY = value[0] == '1';
    } else if (strcmp(key, "leftAction") == 0 || strcmp(key, "middleAction") == 0 || strcmp(key, "rightAction") == 0) {
      ButtonAction parsed;
      if (!parseButtonAction(value, parsed)) {
        error(serial, "INVALID_VALUE");
        return false;
      }
      if (strcmp(key, "leftAction") == 0) config.leftAction = parsed;
      else if (strcmp(key, "middleAction") == 0) config.middleAction = parsed;
      else config.rightAction = parsed;
    } else {
      error(serial, "UNKNOWN_KEY");
      return false;
    }
    statusLedActivity();
    statusLedSetUnsaved(configUnsaved());
    reply(serial, command);
    return true;
  }
  if (strcmp(command, "GET") != 0 && strcmp(command, "RESET") != 0 &&
      strcmp(command, "SAVE") != 0 && strcmp(command, "PING") != 0) {
    error(serial, "UNKNOWN_COMMAND");
    return false;
  }
  if (key) {
    error(serial, "INVALID_ARGUMENTS");
    return false;
  }
  statusLedActivity();
  if (strcmp(command, "PING") == 0) {
    serial.println("@CONFIG {\"ok\":true,\"command\":\"PING\"}");
    return false;
  }
  if (strcmp(command, "SAVE") == 0) {
    statusLedSetSaving(true);
    const ConfigSaveResult result = saveDeviceConfig(config);
    if (result == ConfigSaveResult::Saved) {
      configSetPersistentBaseline(config);
      reply(serial, command);
    }
    else error(serial, result == ConfigSaveResult::InvalidConfig ? "INVALID_CONFIG" : "SAVE_FAILED");
    statusLedSetUnsaved(configUnsaved());
    statusLedSetSaving(false);
    return false;
  }
  const bool reset = strcmp(command, "RESET") == 0;
  if (reset) config = DEFAULT_CONFIG;
  statusLedSetUnsaved(configUnsaved());
  reply(serial, command);
  return reset;
}
} // namespace

DeviceConfig config = DEFAULT_CONFIG;

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
      if (discardLine) error(serial, "INVALID_LINE");
      else if (lineLength) {
        line[lineLength] = '\0';
        if (executeLine(serial)) changed = true;
      }
      lineLength = 0;
      discardLine = false;
      // At most one non-empty line per loop; return to input processing.
      if (hadLine) break;
    } else if (!discardLine) {
      if ((incoming < 32 && incoming != '\t') || incoming > 126 ||
          lineLength >= LINE_CAPACITY - 1) {
        discardLine = true;
      } else {
        line[lineLength++] = static_cast<char>(incoming);
      }
    }
  }
  return changed;
}

namespace {
DeviceConfig persistentBaseline = DEFAULT_CONFIG;
bool equalAction(const ButtonAction &a, const ButtonAction &b) {
  return a.type == b.type && a.code == b.code && a.modifiers == b.modifiers;
}
}
bool equalDeviceConfig(const DeviceConfig &a, const DeviceConfig &b) {
  return a.pointerSensitivity == b.pointerSensitivity && a.middleSensitivity == b.middleSensitivity &&
    a.invertX == b.invertX && a.invertY == b.invertY &&
    equalAction(a.leftAction, b.leftAction) && equalAction(a.middleAction, b.middleAction) &&
    equalAction(a.rightAction, b.rightAction);
}
void configSetPersistentBaseline(const DeviceConfig &value) {
  persistentBaseline = value;
  statusLedSetUnsaved(configUnsaved());
}
bool configUnsaved() { return !equalDeviceConfig(config, persistentBaseline); }
