#pragma once

#include "config.h"

// Boot only, before PS/2 interrupts start. Does not write Flash.
bool loadDeviceConfig(DeviceConfig &value);

enum class ConfigSaveResult { Saved, InvalidConfig, StorageError };
// Main loop SAVE handler only. Never call from ISR or pointer processing.
ConfigSaveResult saveDeviceConfig(const DeviceConfig &value);

// Flash commit pauses interrupts: the sketch must discard partial PS/2 input.
bool takeConfigFlashWrite();
