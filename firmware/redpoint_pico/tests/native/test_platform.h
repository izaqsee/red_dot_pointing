#pragma once
#include <stdint.h>
#include <array>
#include <vector>
extern uint32_t now;
extern bool pressed[3], flashAvailable, failCommit, corruptCommit, ledCanSend, hidCanSend;
extern uint8_t flashImage[4096];
extern unsigned writes, resyncs, ledShows;
extern uint32_t ledColor;
extern uint32_t ledColorAtCommit;
extern std::vector<std::array<int, 3>> mouseReports;
extern std::vector<std::array<uint8_t, 8>> keyReports;
void testReset(void);
void testFrame(uint8_t value, bool parityOkay = true, bool stopOkay = true);
void testPacket(int8_t x, int8_t y);
void testDrain(void);
void runHardwareTests(void);
