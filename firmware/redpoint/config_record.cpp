#include "config_record.h"

#include <math.h>
#include <string.h>
#include <limits>

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x544E5052; // bytes: RPNT
constexpr size_t CRC_OFFSET = CONFIG_RECORD_SIZE - sizeof(uint32_t);
constexpr uint32_t CRC_POLYNOMIAL = 0xEDB88320;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Config format requires IEEE-754 binary32");

void put16(uint8_t *p, uint16_t value) {
  p[0] = value & 0xff;
  p[1] = value >> 8;
}
uint16_t get16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
void put32(uint8_t *p, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) p[i] = (value >> (8 * i)) & 0xff;
}
uint32_t get32(const uint8_t *p) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= uint32_t(p[i]) << (8 * i);
  return value;
}
void putFloat(uint8_t *p, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  put32(p, bits);
}
float getFloat(const uint8_t *p) {
  const uint32_t bits = get32(p);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}
uint32_t crc32(const uint8_t *p) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < CRC_OFFSET; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1) ? CRC_POLYNOMIAL : 0);
    }
  }
  return crc ^ 0xffffffff;
}
} // namespace

bool validDeviceConfig(const DeviceConfig &value) {
  return isfinite(value.pointerSensitivity) && isfinite(value.middleSensitivity) &&
    value.pointerSensitivity >= MIN_SENSITIVITY && value.pointerSensitivity <= MAX_SENSITIVITY &&
    value.middleSensitivity >= MIN_SENSITIVITY && value.middleSensitivity <= MAX_SENSITIVITY;
}

bool encodeConfigRecord(const DeviceConfig &value, uint8_t *record) {
  if (!validDeviceConfig(value)) return false;
  memset(record, 0, CONFIG_RECORD_SIZE);
  put32(record, CONFIG_MAGIC);
  put16(record + 4, CONFIG_FORMAT_VERSION);
  put16(record + 6, CONFIG_RECORD_SIZE);
  putFloat(record + 8, value.pointerSensitivity);
  putFloat(record + 12, value.middleSensitivity);
  record[16] = value.invertX ? 1 : 0;
  record[17] = value.invertY ? 1 : 0;
  put32(record + CRC_OFFSET, crc32(record));
  return true;
}

bool decodeConfigRecord(const uint8_t *record, DeviceConfig &value) {
  value = DEFAULT_CONFIG;
  if (get32(record) != CONFIG_MAGIC || get16(record + 4) != CONFIG_FORMAT_VERSION ||
      get16(record + 6) != CONFIG_RECORD_SIZE || get32(record + CRC_OFFSET) != crc32(record) ||
      record[16] > 1 || record[17] > 1 || record[18] != 0 || record[19] != 0) return false;
  const DeviceConfig decoded = {getFloat(record + 8), getFloat(record + 12),
                                record[16] == 1, record[17] == 1};
  if (!validDeviceConfig(decoded)) return false;
  value = decoded;
  return true;
}
