#pragma once

#include <Arduino.h>
#include "button_action.h"

struct DeviceConfig {
  float pointerSensitivity;
  float middleSensitivity;
  bool invertX;
  bool invertY;
  ButtonAction leftAction;
  ButtonAction middleAction;
  ButtonAction rightAction;
};

constexpr DeviceConfig DEFAULT_CONFIG = {1.00f, 0.40f, false, false, LEFT_ACTION, MIDDLE_ACTION, RIGHT_ACTION};
constexpr float MIN_SENSITIVITY = 0.0f;
constexpr float MAX_SENSITIVITY = 10.0f;

bool validDeviceConfig(const DeviceConfig &value);

extern DeviceConfig config;

// Main loop only. Returns true when SET/RESET succeeds; clear motion remainders.
bool pollConfigSerial(Stream &serial);

// Boot baseline includes defaults and logical v1 migration.
void configSetPersistentBaseline(const DeviceConfig &value);
bool configUnsaved();
bool equalDeviceConfig(const DeviceConfig &a, const DeviceConfig &b);
