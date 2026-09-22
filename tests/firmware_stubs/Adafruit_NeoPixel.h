#pragma once
#include <stdint.h>
constexpr int NEO_GRB = 1, NEO_KHZ800 = 2;
extern int ledShows;
extern uint32_t ledColor;
extern bool ledCanShow;
class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(int, int, int) {}
  bool begin() { return true; }
  void setBrightness(uint8_t) {}
  bool canShow() { return ledCanShow; }
  void setPixelColor(int, uint8_t r, uint8_t g, uint8_t b) { ledColor = (uint32_t(r)<<16) | (uint32_t(g)<<8) | b; }
  void show() { ++ledShows; }
};
