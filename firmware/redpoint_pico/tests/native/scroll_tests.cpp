#include "test_platform.h"
#include "input_runtime.h"
#include "hid_state.h"
#include "config_platform.h"
#include "config.h"
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
void command(const char *s) { ConfigResponse r; executeConfigCommand(s,strlen(s),r); redpoint_config_apply(); }
void button(unsigned i,bool down,bool drain=true) {
  pressed[i]=down; redpoint_input_task(); now+=5; redpoint_input_task(); if(drain) testDrain();
}
void expect(int x,int y,int w,int p,int b=0) {
  assert(!mouseReports.empty()); assert((mouseReports.back()==std::array<int,5>{b,x,y,w,p}));
}
void offlinePacket() {
  now+=4; testFrame(8); testFrame(5); testFrame(6); redpoint_input_task();
}
}
void runScrollTests() {
  testReset(); command("SET wheelSensitivityX 0.5"); command("SET wheelSensitivityY 0.25");
  command("SET pointerInvertX 1"); command("SET pointerInvertY 1"); button(1,true);
  testPacket(1,1); expect(0,0,0,0);
  testPacket(1,1); expect(0,0,0,1);
  testPacket(1,1); expect(0,0,0,0);
  testPacket(1,1); expect(0,0,-1,1); // pointer inversions never reach wheel
  command("SET wheelSensitivityX 0"); command("SET wheelSensitivityY 1");
  testPacket(-2,-3); expect(0,0,2,0);
  command("SET wheelSensitivityX 1"); command("SET wheelSensitivityY 0");
  testPacket(-2,-3); expect(0,0,0,-3);
  command("SET wheelSensitivityY 1");
  testPacket(2,3); expect(0,0,-2,3); // default vertical base polarity only
  command("SET wheelInvertY 1"); testPacket(2,3); expect(0,0,2,3);
  command("SET wheelInvertX 1"); testPacket(2,3); expect(0,0,2,-3);
  button(1,false); testPacket(2,3); expect(-3,-2,0,0);
  command("SET pointerInvertX 0"); command("SET pointerInvertY 0");
  testPacket(2,3); expect(3,2,0,0); // wheel inversions never reach pointer
  command("RESET"); assert(equalDeviceConfig(config, DEFAULT_CONFIG));
  std::cout << "PASS: C.2 independent transforms/sensitivities/zero axes/fractions/default polarity/RESET\n";
  testReset(); testPacket(3,-2); expect(-2,3,0,0);
  command("SET middleSensitivity 1"); button(1,true);
  testPacket(3,-2); expect(0,0,-3,-2); testPacket(-3,2); expect(0,0,3,2);
  command("SET wheelInvertX 1"); testPacket(3,-2); expect(0,0,-3,2);
  command("SET wheelInvertY 1"); testPacket(3,-2); expect(0,0,3,2);
  command("SET middleSensitivity 0"); testPacket(127,127); expect(0,0,0,0);
  command("SET wheelInvertX 0"); command("SET wheelInvertY 0"); command("SET middleSensitivity 0.5");
  testPacket(1,-1); expect(0,0,0,0); testPacket(1,-1); expect(0,0,-1,-1);
  testPacket(-1,1); expect(0,0,0,0); testPacket(-1,1); expect(0,0,1,1);
  command("SET middleSensitivity 1.5"); testPacket(127,-127); expect(0,0,-127,-127);
  testPacket(0,0); expect(0,0,0,0); // no replay of saturated integral excess
  testPacket(1,-1); expect(0,0,-2,-2); // fractional .5 survives saturation
  testPacket(-127,127); expect(0,0,127,127); testPacket(-1,1); expect(0,0,2,2);
  std::cout << "PASS: scroll axes/sensitivity/sign/invert/fraction/clamp carry\n";
  const char *mapping[]={"SET leftAction mouse:middle","SET middleAction mouse:middle","SET rightAction mouse:middle"};
  for(unsigned i=0;i<3;++i) {
    testReset(); command(mapping[i]); command("SET middleSensitivity 1");
    button(i,true); testPacket(2,3); expect(0,0,-2,3);
    button(i,false); testPacket(2,3); expect(3,2,0,0);
    for(auto r:mouseReports) assert(!(r[0]&4));
  }
  testReset(); command(mapping[0]); command(mapping[2]); command("SET middleSensitivity 0.5");
  button(0,true); button(2,true); testPacket(1,1); expect(0,0,0,0);
  button(0,false); assert(mouseActionHeld(MouseButtonCode::Middle));
  testPacket(1,1); expect(0,0,-1,1); // handoff preserves fraction
  command("SET rightAction key:01:04"); button(2,false);
  assert(!mouseActionHeld(MouseButtonCode::Middle)); testPacket(1,1); expect(1,1,0,0);
  button(2,true); button(1,true); testPacket(2,2); expect(0,0,-1,1);
  button(1,false); button(2,false); for(auto b:keyReports.back()) assert(b==0);
  testReset(); command("SET pointerSensitivity 0.5"); command("SET middleSensitivity 0.5");
  testPacket(1,1); auto count=mouseReports.size(); button(1,true); button(1,false);
  assert(mouseReports.size()==count); testPacket(1,1); expect(0,0,0,0);
  button(1,true); testPacket(1,1); expect(0,0,0,0);
  button(1,false); button(1,true); testPacket(1,1); expect(0,0,0,0);
  command("SET middleSensitivity 0.5"); testPacket(1,1); expect(0,0,0,0);
  command("RESET"); command("SET middleSensitivity 0.5"); testPacket(1,1); expect(0,0,0,0);
  testPacket(1,1); expect(0,0,-1,1);
  std::cout << "PASS: physical mappings/refcounts/latch/mode transitions/SET RESET fractions\n";
  testReset(); command("SET middleSensitivity 1"); button(1,true);
  mouseReports.clear(); hidCanSend=false; testPacket(3,4);
  assert(mouseReports.empty()); hidCanSend=true; testDrain(); expect(0,0,-3,4);
  uint8_t current[5]; redpoint_hid_current_mouse(current);
  for(auto byte:current) assert(byte==0); // GET_REPORT never replays scroll
  hidCanSend=false; testPacket(3,4); mouseReports.clear(); redpoint_hid_service(false);
  for(int i=0;i<200;++i) offlinePacket();
  button(1,false,false); hidCanSend=true; testDrain();
  for(auto r:mouseReports) assert((r==std::array<int,5>{0,0,0,0,0}));
  testPacket(2,3); expect(3,2,0,0);
  button(0,true); button(1,true); mouseReports.clear(); hidCanSend=false;
  for(int i=0;i<129;++i) testPacket(1,1);
  hidCanSend=true; testDrain(); assert(mouseReports.size()==2);
  assert((mouseReports.front()==std::array<int,5>{0,0,0,0,0})); expect(0,0,0,0,1);
  testPacket(2,3); expect(0,0,-2,3,1); button(1,false); button(0,false); expect(0,0,0,0);
  testPacket(2,3); expect(3,2,0,0);
  std::cout << "PASS: scroll suspend discard/overflow neutral-current recovery/releases\n";
  testReset();
}
