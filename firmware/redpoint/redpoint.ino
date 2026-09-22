#include <Mouse.h>
#include <Keyboard.h>
#include "config.h"
#include "config_storage.h"
#include "status_led.h"

// ============================================================
// Pin assignment
// Philhower RP2040 coreでは数字 = GPIO番号
// ============================================================

// WZ TrackPoint
constexpr uint8_t CLK_PIN  = 12;   // GP2
constexpr uint8_t DATA_PIN = 13;   // GP3

// External buttons
constexpr uint8_t BTN_L_PIN = 3;  // GP4
constexpr uint8_t BTN_M_PIN = 2;  // GP5
constexpr uint8_t BTN_R_PIN = 4;  // GP6

// ============================================================
// Settings
// ============================================================

// 同一packet内のbyte間隔より十分長い値。
// packet間のgapを見つけるために使用。
constexpr uint32_t PACKET_GAP_MS = 4;

// タクトスイッチのデバウンス
constexpr uint32_t DEBOUNCE_MS = 5;

// デバッグ出力
#define DEBUG_INPUTS 1

// 1なら全pointer packetを表示。
// 2なら2 packetに1回、5なら5 packetに1回。
constexpr uint8_t DEBUG_POINTER_EVERY_N = 1;

// ============================================================
// PS/2 byte FIFO
// ============================================================

struct Ps2Byte {
  uint8_t data;
  bool packetStart;
};

constexpr uint8_t FIFO_SIZE = 128;

volatile Ps2Byte fifo[FIFO_SIZE];
volatile uint8_t fifoHead = 0;
volatile uint8_t fifoTail = 0;

volatile uint32_t droppedBytes = 0;

// ============================================================
// PS/2 frame decoder
//
// 11 bits:
//
// start
// D0
// D1
// ...
// D7
// parity
// stop
//
// ============================================================

volatile uint8_t bitIndex = 0;
volatile uint8_t dataByte = 0;
volatile uint8_t parityBit = 0;

volatile uint32_t lastByteMs = 0;
volatile bool haveLastByte = false;

// ============================================================
// Button state
// ============================================================

struct ButtonState {
  uint8_t pin;
  ButtonAction activeAction;

  bool rawPressed;
  bool stablePressed;

  uint32_t changedAt;
};

ButtonState buttons[3] = {
  { BTN_L_PIN, DISABLED_ACTION, false, false, 0 },
  { BTN_M_PIN, DISABLED_ACTION, false, false, 0 },
  { BTN_R_PIN, DISABLED_ACTION, false, false, 0 }
};

// ============================================================
// Pointer fractional remainders
// ============================================================

float scrollAccX = 0.0f;
float scrollAccY = 0.0f;
float pointerAccX = 0.0f;
float pointerAccY = 0.0f;

// ============================================================
// FIFO
// ============================================================

void pushByte(uint8_t b, bool packetStart) {
  uint8_t next = (fifoHead + 1) % FIFO_SIZE;

  if (next == fifoTail) {
    droppedBytes++;
    return;
  }

  fifo[fifoHead].data = b;
  fifo[fifoHead].packetStart = packetStart;
  fifoHead = next;
}

bool popByte(Ps2Byte &out) {
  if (fifoTail == fifoHead) {
    return false;
  }

  noInterrupts();

  out.data = fifo[fifoTail].data;
  out.packetStart = fifo[fifoTail].packetStart;

  fifoTail = (fifoTail + 1) % FIFO_SIZE;

  interrupts();

  return true;
}

// ============================================================
// PS/2 CLK interrupt
// ============================================================

void clkISR() {
  uint8_t bit = digitalRead(DATA_PIN);

  // ----------------------------------------------------------
  // Start bit
  // ----------------------------------------------------------

  if (bitIndex == 0) {
    if (bit != 0) {
      return;
    }

    dataByte = 0;
    bitIndex = 1;
    return;
  }

  // ----------------------------------------------------------
  // Data bits D0-D7
  // ----------------------------------------------------------

  if (bitIndex >= 1 && bitIndex <= 8) {
    if (bit) {
      dataByte |= (1 << (bitIndex - 1));
    }

    bitIndex++;
    return;
  }

  // ----------------------------------------------------------
  // Odd parity
  // ----------------------------------------------------------

  if (bitIndex == 9) {
    parityBit = bit;
    bitIndex++;
    return;
  }

  // ----------------------------------------------------------
  // Stop bit
  // ----------------------------------------------------------

  if (bitIndex == 10) {
    bitIndex = 0;

    // stop must be HIGH
    if (bit != 1) {
      return;
    }

    // Odd parity check
    uint8_t ones = parityBit;

    for (int i = 0; i < 8; i++) {
      ones += (dataByte >> i) & 1;
    }

    if ((ones & 1) == 0) {
      return;
    }

    // --------------------------------------------------------
    // packet boundary detection
    // --------------------------------------------------------

    uint32_t now = millis();

    bool packetStart =
      !haveLastByte ||
      ((uint32_t)(now - lastByteMs) >= PACKET_GAP_MS);

    lastByteMs = now;
    haveLastByte = true;

    pushByte(dataByte, packetStart);
  }
}

// ============================================================
// TrackPoint packet sanity check
// ============================================================

bool packetLooksSane(
  uint8_t status,
  uint8_t x,
  uint8_t y
) {
  bool xs = (status & 0x10) != 0;
  bool ys = (status & 0x20) != 0;

  bool xmsb = (x & 0x80) != 0;
  bool ymsb = (y & 0x80) != 0;

  // status内のsign bitと実データのMSBが一致すること
  return (xs == xmsb) && (ys == ymsb);
}

// ============================================================
// Buttons
// ============================================================

void updateButtons() {
  uint32_t now = millis();

  for (int i = 0; i < 3; i++) {

    // INPUT_PULLUPなので
    // LOW = pressed
    bool pressed = (digitalRead(buttons[i].pin) == LOW);

    // 生状態が変わった
    if (pressed != buttons[i].rawPressed) {
      buttons[i].rawPressed = pressed;
      buttons[i].changedAt = now;
    }

    // DEBOUNCE_MS以上安定したら正式採用
    if (
      pressed != buttons[i].stablePressed &&
      (uint32_t)(now - buttons[i].changedAt) >= DEBOUNCE_MS
    ) {
      buttons[i].stablePressed = pressed;

      if (pressed) {
        const ButtonAction &action = i == 0 ? config.leftAction : i == 1 ? config.middleAction : config.rightAction;
        pressButtonAction(buttons[i].activeAction, action);
      } else {
        releaseButtonAction(buttons[i].activeAction);
      }
      #if DEBUG_INPUTS
      const char* name =
        (i == 0) ? "LEFT" :
        (i == 1) ? "MIDDLE" :
                  "RIGHT";

      Serial.print("@DEBUG BTN ");
      Serial.print(name);
      Serial.println(pressed ? " DOWN" : " UP");
      #endif
    }
  }
}

// ============================================================
// TrackPoint packet → USB Mouse
//
// 実機確認済みの軸変換: USB X = PS/2 Y、USB Y = PS/2 X。
// ============================================================

void handleTrackPointPacket(
  uint8_t status,
  uint8_t xb,
  uint8_t yb
) {
  int16_t dx = (int8_t)xb;
  int16_t dy = (int8_t)yb;

  // 現在の軸変換を維持
  int16_t usbX = dy;
  int16_t usbY = dx;

  // Mouse.move()へ安全に渡せる範囲へ
  if (usbX > 127)  usbX = 127;
  if (usbX < -127) usbX = -127;

  if (usbY > 127)  usbY = 127;
  if (usbY < -127) usbY = -127;

  if (config.invertX) usbX = -usbX;
  if (config.invertY) usbY = -usbY;

  // ----------------------------------------------------------
  // Middle button held:
  // pointer sensitivity reduction for autoscroll
  // ----------------------------------------------------------

  int16_t outX;
  int16_t outY;

  const bool middleHeld = mouseActionHeld(MouseButtonCode::Middle);
  if (middleHeld) {

    pointerAccX = 0.0f;
    pointerAccY = 0.0f;

    // 小さい移動量を捨てないよう、小数部を蓄積する
    scrollAccX += usbX * config.middleSensitivity;
    scrollAccY += usbY * config.middleSensitivity;

    outX = (int16_t)scrollAccX;
    outY = (int16_t)scrollAccY;

    scrollAccX -= outX;
    scrollAccY -= outY;

  } else {

    // default 1.00では従来と同一。小数感度の場合だけ残量が生じる。
    pointerAccX += usbX * config.pointerSensitivity;
    pointerAccY += usbY * config.pointerSensitivity;
    outX = (int16_t)pointerAccX;
    outY = (int16_t)pointerAccY;
    pointerAccX -= outX;
    pointerAccY -= outY;

    // スクロールモードを抜けたら残りを捨てる
    scrollAccX = 0.0f;
    scrollAccY = 0.0f;
  }

  // 高感度でもint8_tへの変換がwrapしないよう飽和させる。
  // 飽和した整数分は持ち越さず、小数部のみ保持する。
  if (outX > 127) outX = 127;
  if (outX < -127) outX = -127;
  if (outY > 127) outY = 127;
  if (outY < -127) outY = -127;

#if DEBUG_INPUTS
  static uint32_t debugPacketCount = 0;
  debugPacketCount++;

  if ((debugPacketCount % DEBUG_POINTER_EVERY_N) == 0) {
    Serial.print("@DEBUG PTR ");

    Serial.print("raw=(");
    Serial.print(dx);
    Serial.print(",");
    Serial.print(dy);
    Serial.print(")");

    Serial.print(" usb=(");
    Serial.print(usbX);
    Serial.print(",");
    Serial.print(usbY);
    Serial.print(")");

    Serial.print(" out=(");
    Serial.print(outX);
    Serial.print(",");
    Serial.print(outY);
    Serial.print(")");

    Serial.print(" middle=");
    Serial.print(middleHeld ? 1 : 0);

    Serial.print(" S=0x");
    if (status < 0x10) Serial.print("0");
    Serial.print(status, HEX);

    Serial.print(" drop=");
    Serial.println(droppedBytes);
  }
#endif

  Mouse.move(
    (int8_t)outX,
    (int8_t)outY,
    0
  );
}

// ============================================================
// Setup
// ============================================================

void setup() {
  statusLedBegin();

  // Load once before PS/2 IRQ/HID initialization; invalid storage uses defaults.
  loadDeviceConfig(config);
  configSetPersistentBaseline(config);

  // ----------------------------------------------------------
  // USB Serial
  //
  // COM経由の自動書き込み用にも残しておく。
  // ----------------------------------------------------------

  Serial.begin(115200);

  // ----------------------------------------------------------
  // Buttons
  // ----------------------------------------------------------

  pinMode(BTN_L_PIN, INPUT_PULLUP);
  pinMode(BTN_M_PIN, INPUT_PULLUP);
  pinMode(BTN_R_PIN, INPUT_PULLUP);

  for (int i = 0; i < 3; i++) {
    bool pressed = (digitalRead(buttons[i].pin) == LOW);

    buttons[i].rawPressed = pressed;
    buttons[i].stablePressed = pressed;
    buttons[i].changedAt = millis();
  }

  // ----------------------------------------------------------
  // PS/2
  // ----------------------------------------------------------

  pinMode(CLK_PIN, INPUT);
  pinMode(DATA_PIN, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(CLK_PIN),
    clkISR,
    FALLING
  );

  // ----------------------------------------------------------
  // USB HID Mouse
  // ----------------------------------------------------------

  Mouse.begin();
  Keyboard.begin(); // Pico SDK stack: composite CDC + Mouse + Keyboard.

  delay(1000);

  // Latch startup-held buttons only after the USB devices are initialized.
  for (int i = 0; i < 3; ++i) {
    if (buttons[i].stablePressed && digitalRead(buttons[i].pin) == LOW) {
      const ButtonAction &action = i == 0 ? config.leftAction : i == 1 ? config.middleAction : config.rightAction;
      pressButtonAction(buttons[i].activeAction, action);
    }
  }

#if DEBUG_INPUTS
  Serial.println("@DEBUG WZ RP2040 mouse ready");
#endif
  statusLedEndBoot();
}

// ============================================================
// Main loop
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // Independent buttons
  // ----------------------------------------------------------

  updateButtons();

  // ----------------------------------------------------------
  // TrackPoint packet parser
  // ----------------------------------------------------------

  static uint8_t packet[3];
  static uint8_t packetIndex = 0;
  static bool synced = false;

  Ps2Byte b;

  while (popByte(b)) {

    // 長いgapがあった = 新packetの先頭
    if (b.packetStart) {
      packetIndex = 0;
      synced = true;
    }

    if (!synced) {
      continue;
    }

    packet[packetIndex++] = b.data;

    // --------------------------------------------------------
    // 3-byte TrackPoint packet
    // --------------------------------------------------------

    if (packetIndex == 3) {

      if (
        packetLooksSane(
          packet[0],
          packet[1],
          packet[2]
        )
      ) {

        handleTrackPointPacket(
          packet[0],
          packet[1],
          packet[2]
        );

        packetIndex = 0;

      } else {

        // alignmentを失ったら、
        // 次のpacket gapまで待つ
        synced = false;
        packetIndex = 0;
      }
    }
  }

  // FIFO処理後、待機せず固定量だけSerial入力を処理する。
  if (pollConfigSerial(Serial)) {
    scrollAccX = scrollAccY = 0.0f;
    pointerAccX = pointerAccY = 0.0f;
  }

  if (takeConfigFlashWrite()) {
    // EEPROM commit pauses IRQs. Discard partial input and wait for a fresh gap.
    // No changes to the ISR decoder, normal packet processing or accumulators.
    noInterrupts();
    fifoTail = fifoHead;
    bitIndex = 0;
    dataByte = 0;
    parityBit = 0;
    lastByteMs = millis();
    haveLastByte = true;
    interrupts();
    packetIndex = 0;
    synced = false;
  }
  statusLedUpdate();
}
