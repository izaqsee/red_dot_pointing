#pragma once
#include <Arduino.h>
// Serial framing adapter. All execution lives in executeConfigCommand().
bool pollConfigSerial(Stream &serial);
