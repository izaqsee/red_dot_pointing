#pragma once

#include "config.h"
#include <stddef.h>
#include <stdint.h>

// v1: 24 B; v2: 36 B; v3 appends Wheel Y / inversions before CRC at offset 40.
// Explicit little-endian encoding; no raw struct padding/bool representation.
constexpr size_t CONFIG_RECORD_V1_SIZE = 24;
constexpr size_t CONFIG_RECORD_V2_SIZE = 36;
constexpr size_t CONFIG_RECORD_SIZE = 44;
constexpr uint16_t CONFIG_FORMAT_VERSION = 3;

bool encodeConfigRecord(const DeviceConfig &value, uint8_t *record);
// Always initializes value to defaults, including when validation fails.
bool decodeConfigRecord(const uint8_t *record, DeviceConfig &value, size_t size = CONFIG_RECORD_SIZE);
