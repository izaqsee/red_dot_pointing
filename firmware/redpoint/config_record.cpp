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
uint32_t crc32(const uint8_t *p, size_t length) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < length; ++i) {
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
    value.middleSensitivity >= MIN_SENSITIVITY && value.middleSensitivity <= MAX_SENSITIVITY &&
    validButtonAction(value.leftAction) && validButtonAction(value.middleAction) && validButtonAction(value.rightAction);
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
  const ButtonAction actions[] = {value.leftAction, value.middleAction, value.rightAction};
  for (size_t i = 0; i < 3; ++i) {
    const size_t offset = 20 + i * 4;
    record[offset] = static_cast<uint8_t>(actions[i].type);
    record[offset + 1] = actions[i].code;
    record[offset + 2] = actions[i].modifiers;
  }
  put32(record + CRC_OFFSET, crc32(record, CRC_OFFSET));
  return true;
}

bool decodeConfigRecord(const uint8_t *record, DeviceConfig &value, size_t size) {
  value = DEFAULT_CONFIG;
  if (size < CONFIG_RECORD_V1_SIZE || get32(record) != CONFIG_MAGIC) return false;
  const uint16_t version = get16(record + 4);
  if (version != 1 && version != CONFIG_FORMAT_VERSION) return false;
  const size_t length = version == 1 ? CONFIG_RECORD_V1_SIZE : CONFIG_RECORD_SIZE;
  const size_t crcOffset = length - sizeof(uint32_t);
  if (size < length || get16(record + 6) != length || get32(record + crcOffset) != crc32(record, crcOffset) ||
      record[16] > 1 || record[17] > 1 || record[18] != 0 || record[19] != 0) return false;
  DeviceConfig decoded = DEFAULT_CONFIG; // v1 migration adds default actions in RAM only.
  decoded.pointerSensitivity = getFloat(record + 8);
  decoded.middleSensitivity = getFloat(record + 12);
  decoded.invertX = record[16] == 1;
  decoded.invertY = record[17] == 1;
  if (version == CONFIG_FORMAT_VERSION) {
    ButtonAction *actions[] = {&decoded.leftAction, &decoded.middleAction, &decoded.rightAction};
    for (size_t i = 0; i < 3; ++i) {
      const size_t offset = 20 + i * 4;
      if (record[offset + 3] != 0) return false;
      *actions[i] = {static_cast<ActionType>(record[offset]), record[offset + 1], record[offset + 2]};
    }
  }
  if (!validDeviceConfig(decoded)) return false;
  value = decoded;
  return true;
}
