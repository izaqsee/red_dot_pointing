#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Models the core's RAM staging buffer and a separate persistent Flash sector.
class MockEEPROM {
public:
  std::array<uint8_t, 256> flash{}, ram{};
  int commits = 0, writes = 0;
  bool commitFails = false, corruptCommit = false;
  size_t size = 0;
  void begin(size_t) { size = ram.size(); ram = flash; }
  size_t length() { return size; }
  uint8_t read(int address) { return ram.at(address); }
  void write(int address, uint8_t value) { ++writes; ram.at(address) = value; }
  bool commit() {
    ++commits;
    if (commitFails) return false;
    flash = ram;
    if (corruptCommit) flash[8] ^= 1;
    return true;
  }
};
extern MockEEPROM EEPROM;
