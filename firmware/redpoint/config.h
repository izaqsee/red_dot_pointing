#pragma once

#include <Arduino.h>

struct DeviceConfig {
  float pointerSensitivity;
  float middleSensitivity;
  bool invertX;
  bool invertY;
};

extern DeviceConfig config;

// Main loop only. Returns true when SET/RESET succeeds; clear motion remainders.
bool pollConfigSerial(Stream &serial);
