#include "test_platform.h"
#include "input_runtime.h"
#include "hid_state.h"
#include "config_platform.h"
#include "config.h"
#include "input_response.h"
#include <cmath>
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
// Existing C.1/C.2 unit-scale expectations; explicit response cancels tick scale.
// Separate transfer tests below check real identity defaults against main's 0.1.
void resetLegacyScroll() { testReset(); config.wheelLowGain = config.wheelHighGain = 10; }
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
  resetLegacyScroll(); command("SET wheelSensitivityX 0.5"); command("SET wheelSensitivityY 0.25");
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
  resetLegacyScroll(); testPacket(3,-2); expect(-2,3,0,0);
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
    resetLegacyScroll(); command(mapping[i]); command("SET middleSensitivity 1");
    button(i,true); testPacket(2,3); expect(0,0,-2,3);
    button(i,false); testPacket(2,3); expect(3,2,0,0);
    for(auto r:mouseReports) assert(!(r[0]&4));
  }
  resetLegacyScroll(); command(mapping[0]); command(mapping[2]); command("SET middleSensitivity 0.5");
  button(0,true); button(2,true); testPacket(1,1); expect(0,0,0,0);
  button(0,false); assert(mouseActionHeld(MouseButtonCode::Middle));
  testPacket(1,1); expect(0,0,-1,1); // handoff preserves fraction
  command("SET rightAction key:01:04"); button(2,false);
  assert(!mouseActionHeld(MouseButtonCode::Middle)); testPacket(1,1); expect(1,1,0,0);
  button(2,true); button(1,true); testPacket(2,2); expect(0,0,-1,1);
  button(1,false); button(2,false); for(auto b:keyReports.back()) assert(b==0);
  resetLegacyScroll(); command("SET pointerSensitivity 0.5"); command("SET middleSensitivity 0.5");
  testPacket(1,1); auto count=mouseReports.size(); button(1,true); button(1,false);
  assert(mouseReports.size()==count); testPacket(1,1); expect(0,0,0,0);
  button(1,true); testPacket(1,1); expect(0,0,0,0);
  button(1,false); button(1,true); testPacket(1,1); expect(0,0,0,0);
  command("SET middleSensitivity 0.5"); testPacket(1,1); expect(0,0,0,0);
  command("RESET"); config.wheelLowGain = config.wheelHighGain = 10; command("SET middleSensitivity 0.5"); testPacket(1,1); expect(0,0,0,0);
  testPacket(1,1); expect(0,0,-1,1);
  std::cout << "PASS: physical mappings/refcounts/latch/mode transitions/SET RESET fractions\n";
  resetLegacyScroll(); command("SET middleSensitivity 1"); button(1,true);
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
  resetLegacyScroll();
}

void runTransferTests() {
  // Exact differential oracle from main 6f83848, including fractions and saturation.
  auto bounded = [](int v) { return v > 127 ? 127 : v < -127 ? -127 : v; };
  for (bool wheel : {false, true}) for (float gain : {0.f, .001f, .4f, 1.25f, 10.f})
    for (int invert = 0; invert < 4; ++invert) {
      testReset(); config.pointerSensitivity = config.wheelSensitivityX = config.wheelSensitivityY = gain;
      config.pointerInvertX = config.wheelInvertX = invert & 1;
      config.pointerInvertY = config.wheelInvertY = invert & 2;
      if (wheel) button(1,true);
      float ax = 0, ay = 0;
      for (int i = 0; i < 512; ++i) {
        const int rawX = (i * 17 % 256) - 128, rawY = (i * 31 % 256) - 128;
        int x = bounded(rawY), y = bounded(rawX);
        if (invert & 1) x = -x;
        if (wheel ? !(invert & 2) : bool(invert & 2)) y = -y;
        ax += wheel ? x * gain * .1f : x * gain;
        ay += wheel ? y * gain * .1f : y * gain;
        int ox = int(ax), oy = int(ay); ax -= ox; ay -= oy;
        testPacket(rawX, rawY);
        if (wheel) expect(0,0,bounded(oy),bounded(ox));
        else expect(bounded(ox),bounded(oy),0,0);
      }
    }
  for (int direction = 0; direction < 4; ++direction) {
    testReset();
    float *gains[] = {&config.directionalGainLeft,&config.directionalGainRight,&config.directionalGainUp,&config.directionalGainDown};
    *gains[direction] = 2;
    const int xs[] = {-3,3,0,0}, ys[] = {0,0,-3,3};
    for (bool wheel : {false,true}) for (int invert=0;invert<4;++invert) {
      config.pointerInvertX=config.wheelInvertX=invert&1;
      config.pointerInvertY=config.wheelInvertY=invert&2;
      config.wheelSensitivityX=config.wheelSensitivityY=10;
      if (wheel && !mouseActionHeld(MouseButtonCode::Middle)) button(1,true);
      for (int d=0;d<4;++d) {
        redpoint_input_config_changed();
        int x=xs[d]*(d==direction?2:1),y=ys[d]*(d==direction?2:1);
        if(invert&1)x=-x;
        if(wheel ? !(invert&2) : bool(invert&2))y=-y;
        testPacket(ys[d],xs[d]);
        if(wheel)expect(0,0,y,x);else expect(x,y,0,0);
      }
    }
  }
  for (float low : {0.f,.25f,1.f,10.f}) for(float high : {0.f,.25f,1.f,10.f}) {
    float previous=low;
    for(int i=0;i<=12700;++i) {
      float x=i/100.f, g=wheelResponseGain(x,-x/2,low,high);
      assert(g >= fminf(low,high)-.00001f && g <= fmaxf(low,high)+.00001f);
      assert(high>=low ? g>=previous-.00001f : g<=previous+.00001f);
      assert(fabsf(g-previous)<.002f); // continuity bound for a 0.01-count step
      if(low==high)assert(g==low);
      assert(g==wheelResponseGain(-x/2,x,low,high));
      previous=g;
    }
    assert(wheelResponseGain(127,0,low,high)==high);
    assert(wheelResponseGain(1270,0,low,high)==high);
  }
  testReset(); config.wheelLowGain=.5f;config.wheelHighGain=2;
  config.wheelSensitivityX=config.wheelSensitivityY=1;
  button(1,true); float ax=0,ay=0;
  for(int i=0;i<100;++i) {
    const float g=wheelResponseGain(30,60,.5f,2);
    ax+=30*g*.1f; ay-=60*g*.1f;
    int x=int(ax),y=int(ay);ax-=x;ay-=y;
    testPacket(60,30);expect(0,0,y,x);
  }
  testReset();
  config.directionalGainLeft=config.directionalGainRight=config.directionalGainUp=config.directionalGainDown=10;
  config.wheelLowGain=config.wheelHighGain=config.wheelSensitivityX=config.wheelSensitivityY=10;
  button(1,true);testPacket(127,-127);expect(0,0,-127,-127);
  testPacket(0,0);expect(0,0,0,0);
  testReset();
  std::cout << "PASS: identity differential 20480 packets, four physical directions before invert, common smooth gain, fractions/polarity\n";
}
