#pragma once

#include <stddef.h>
#include "button_action.h"

struct DeviceConfig {
  float pointerSensitivity;
  float wheelSensitivityX;
  float wheelSensitivityY;
  bool wheelInvertX;
  bool wheelInvertY;
  bool pointerInvertX;
  bool pointerInvertY;
  ButtonAction leftAction;
  ButtonAction middleAction;
  ButtonAction rightAction;
};

constexpr DeviceConfig DEFAULT_CONFIG = {1.00f, 0.40f, 0.40f, false, false, false, false, LEFT_ACTION, MIDDLE_ACTION, RIGHT_ACTION};
constexpr float MIN_SENSITIVITY = 0.0f;
constexpr float MAX_SENSITIVITY = 10.0f;

bool validDeviceConfig(const DeviceConfig &value);

extern DeviceConfig config;

// Boot baseline includes defaults and logical v1/v2 migration.
void configSetPersistentBaseline(const DeviceConfig &value);
bool configUnsaved();
bool equalDeviceConfig(const DeviceConfig &a, const DeviceConfig &b);

constexpr size_t CONFIG_LINE_CAPACITY = 96;
struct ConfigResponse { char text[512]; size_t length; };
// Single main-loop owner only: never execute from IRQ or a concurrent network thread.
bool executeConfigCommand(const char *data, size_t length, ConfigResponse &response);
bool takeConfigChange();
