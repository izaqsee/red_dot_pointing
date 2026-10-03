#pragma once
#include "config.h"
#include <math.h>

// Post-swap device coordinates match uninverted pointer: +X right, +Y down.
struct CorrectedInput { float x, y; };
inline CorrectedInput applyDirectionalCalibration(float x, float y, const DeviceConfig &c) {
  return {x * (x < 0 ? c.directionalGainLeft : c.directionalGainRight),
          y * (y < 0 ? c.directionalGainUp : c.directionalGainDown)};
}
inline float wheelResponseGain(float x, float y, float low, float high) {
  // Runtime clamps signed PS/2 bytes to [-127,127] before calibration.
  // Corrected magnitude drives both axes; calibration can reach the plateau early.
  const float t = fminf(1.0f, fmaxf(fabsf(x), fabsf(y)) / 127.0f);
  const float smooth = t * t * (3.0f - 2.0f * t);
  return low + (high - low) * smooth;
}
// HID wheel ticks per corrected count at unit response/axis gains.
constexpr float WHEEL_BASE_SCALE = 0.1f;
