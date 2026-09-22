#pragma once

#include "config.h"
#include <stddef.h>
#include <stdint.h>

// Explicit little-endian encoding; no raw struct padding/bool representation.
constexpr size_t CONFIG_RECORD_V1_SIZE = 24;
constexpr size_t CONFIG_RECORD_SIZE = 36;
constexpr uint16_t CONFIG_FORMAT_VERSION = 2;

bool encodeConfigRecord(const DeviceConfig &value, uint8_t *record);
// Always initializes value to defaults, including when validation fails.
bool decodeConfigRecord(const uint8_t *record, DeviceConfig &value, size_t size = CONFIG_RECORD_SIZE);
