#pragma once

#include <Arduino.h>

struct DeviceConfig {
  float pointerSensitivity;
  float middleSensitivity;
  bool invertX;
  bool invertY;
};

constexpr DeviceConfig DEFAULT_CONFIG = {1.00f, 0.40f, false, false};
constexpr float MIN_SENSITIVITY = 0.0f;
constexpr float MAX_SENSITIVITY = 10.0f;

bool validDeviceConfig(const DeviceConfig &value);

extern DeviceConfig config;

// Main loop only. Returns true when SET/RESET succeeds; clear motion remainders.
bool pollConfigSerial(Stream &serial);
