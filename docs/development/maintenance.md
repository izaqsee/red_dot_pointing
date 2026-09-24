# 実装・保守メモ

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

仕様のsource of truthは[protocol](../protocol.md)、[USB Ethernet](../usb-ethernet.md)、
[status LED検証](../status-led-validation.md)です。以下はREADMEに混在していた詳細を保全した補足資料です。

適用範囲: Flash v3・explicit SAVEの説明は現行の共通codec向けです。
LEDのAdafruit NeoPixel依存とIRQ注意事項はArduino版向けで、Pico SDK backendはPIO実装です。
旧「全7項目」は現在の10項目schemaに更新されています。旧firmwareのSAVE未実装やWebHID移行案は履歴・将来案です。
旧README概要にあった「加速度設定」は現行schemaにはなく、acceleration curve editorも未実装です。
Middle押下中の低感度X/YはArduino版の履歴で、Pico C.1以降はnative Wheel/Panです。

すべてのコマンドは、特記がなければrepository rootで実行します。
[開発資料一覧](README.md) · [プロジェクト概要](../../README.md)

### Runtime設定とFlash保存

- **SET**: 実機の動作へ即時反映するRAM設定。Flashを書き換えない。
- **SAVE**: 現在の設定をFlashへ明示保存し、再起動後も読み込む。
- **RESET**: RAMのみdefaultに戻す。SAVEしなければ再起動後は以前の保存値に戻る。
- **RESET → SAVE**: defaultをFlashへ保存する。
- **未保存・不正な保存データ**: 起動時に全設定をdefaultへfallbackする。

共通codecは44-byte v3でPointer/Wheel独立設定とボタン割当、CRC32を保存します。
Pico SDK版は既存sector `0x10FFF000–0x10FFFFFF`、Arduino版はEEPROM emulationを使用します。
v1/v2をRAM上で移行し、次の明示Saveでv3へ更新します。不正recordは全defaultへ戻ります。
詳細は[通信仕様](../../docs/protocol.md#flash保存形式と起動)と[Pico C.2報告](../../firmware/redpoint_pico/MILESTONE_C2.md)を参照してください。
SET、RESET、起動時はFlashを書かず、SAVE時も前回保存内容と同一ならerase/writeを省略します。

Save成功はデバイスでのcommitとFlash再読込照合が完了した後に通知されます。
UIは応答で確認した値を**Saved**とし、設定を変えると**Unsaved changes**にします。
同じ接続中に保存確認済みの値へ戻した場合もSavedになります。
再接続時のGETはRAMだけを取得するので**保存状態未確認**と表示し、Flash保存済みとは推測しません。
設定値をlocalStorageへ保存しません。SAVEの失敗・timeoutはSaved扱いにせず再試行できます。
旧firmwareではSAVEがNOT_IMPLEMENTEDになります。永続化には更新後のfirmwareが必要です。

保存中はFlash操作のため割り込みとHID/PS/2処理が短時間停止します。
デバイスを静止させてSaveし、完了までUSBを抜かないでください。
書込み後は途中frame/FIFOを破棄してpacket gapで同期を取り直します。
単一sectorなので、保存途中の電源断で以前の保存内容が失われる可能性があります。
不正なデータは次回起動時にdefaultへfallbackします。二重化・wear levelingは未実装です。
EEPROM領域やFlash容量の変更、全消去を伴う書き込みでは保存値が失われる場合があります。

設定値・Serial dataの外部送信、analytics、localStorageへの保存は行いません。
すべて同梱のローカルassetを使い、CSPはsame-originへのHTTP API通信だけを許可します。外部へのデバイスデータ送信は行いません。
GitHub Pages版でもデバイスデータは外部serverへ送りません。GitHubには静的ページの通常の取得だけが発生します。
既存のPages workflowをそのまま利用します。この変更ではpush／deployやGitHub側の設定変更は行いません。


### 通信とエラー時の動作

通信仕様のsource of truthは[docs/protocol.md](../../docs/protocol.md)です。
GET / SET / RESET / SAVEの形式を維持し、configへleftAction / middleAction / rightActionを追加しています。

- 115200 baud / 8N1 / flow controlなし。`@CONFIG`と`@DEBUG`を分類する。
- 分割された受信chunkを行へ復元する。debug・未知行・不正JSONは設定へ混ぜず無視する。
- `@CONFIG`の型・範囲を検証し、現在の要求に対応する応答だけを確定値にする。
- 120msのdebounceと項目ごとの最新値への集約を行い、要求は必ず1つずつ送る。
- RESETは未送信変更を破棄し、送信中SETの応答を待ってから実行する。
- SAVEは未送信SETをすべて反映してから送る。SET失敗時は保留SAVEも中止し、保存を誤認させない。
- 接続済みsessionの2秒のtimeout時は、適用済みか不明なので未送信変更を破棄し、改行＋GETで再同期する。
  遅れて届くSET/RESET/SAVE応答やエラーは再同期GETの応答に使わない。
  GETだけでは保存成功を判定できないため保存状態は未確認とし、必要ならSaveを再実行する。
  再同期にも失敗した場合は切断し、再接続を案内する。
- 切断時は保留要求を中止し、stream lockを解放してポートを閉じる。
  USB抜去・read/writeエラー後もConnectからやり直せる。

`app.js`内のprotocol処理、Serial transport、UIは関数単位で分離しています。
WebHID移行時はtransportを差し替える設計ですが、HID reportの設計・firmware対応は今後必要です。
Web SerialのAPI利用は[Chrome公式資料](https://developer.chrome.com/docs/capabilities/serial)を参照しています。


### 次phase候補

- 実機での長時間操作・debug負荷を含む検証とUX調整
- 保存途中の電源断対策（二重化）、書込寿命の評価
- 必要になった段階でのWebHID用transport / firmware report設計

firmware update、acceleration curve editor、macro、multi-step chord、long press、double click、layerは未実装です。


### Onboard Device Status Indicator

VCC-GND YD RP2040のWS2812（GPIO23、1 pixel、GRB / 800 kHz）を使用します。
**RGB solder jumperのbridgeが必要**です。通常の単色LEDとは別です。
眩しさを抑えるため`status_led.h`の`STATUS_LED_BRIGHTNESS`は8（255段階）です。
既存環境の **Adafruit NeoPixel 1.15.5** を使用します。他環境でのビルドにもこのライブラリが必要です。
Philhower RP2040 core 6.1.0、Pico SDK USB stackは維持しています。

| 色 | 意味 |
| --- | --- |
| WHITE | Boot（既存の起動待ち時間中） |
| BLUE | Normal |
| GREEN | Configurator active（最近6秒以内の有効な設定通信） |
| YELLOW | Unsaved changes |
| PURPLE | Saving（LED表示のみ最低150 ms） |
| RED | Runtime error（最後のエラーから2秒） |

Bootは起動時だけのoverrideです。通常時の優先順位は
**Error > Saving > Unsaved > Configurator active > Normal**です。
Unsavedはfirmwareが全7項目を起動時／最後のSAVE成功時のlogical configと比較します。
元の値へ戻せばSaved相当に戻り、RESETも比較結果に従います。SAVE失敗ではbaselineを変更しません。
default fallbackやv1 migrationだけではYELLOW／REDにしません。

ConfiguratorはGET同期後にPING capabilityをprobeし、対応時のみidle中に2秒間隔で送信します。
Disconnect／unplug時はtimerを停止します。LEDの接続解除反映は最後の通信から最大6秒です。
Unsavedなら切断・再接続してもYELLOWを維持します。旧firmwareのUNKNOWN_COMMANDは表示せず、
そのsessionのheartbeatだけを無効化して通常の設定機能を継続します。
詳細は[protocol](../../docs/protocol.md)と[実機確認手順](../../docs/status-led-validation.md)を参照してください。

LED送信はsetup／main loopの色変化時だけです。PIO使用のNeoPixelドライバにも短いIRQ停止区間があるため、
PS/2への無影響はhostテストだけでは保証できません。1 pixelの線上送信は約30 µsで、
CPUのIRQ停止時間とは同一ではありません。ラッチ待ち中はSerial処理を次loopへ譲り、HID／PS/2処理を続けます。
Flash commitの既存のIRQ停止と受信再同期は従来どおりです。

## EN

The authoritative specifications are [protocol](../protocol.md), [USB Ethernet](../usb-ethernet.md), and [status LED validation](../status-led-validation.md). This preserves implementation details previously mixed into the README.

Scope: Flash v3 and explicit SAVE describe the current shared codec. Adafruit NeoPixel/IRQ notes are Arduino-specific; the Pico SDK backend uses PIO. Historical “all seven fields” is now a ten-field schema. Unsupported SAVE in old firmware and possible WebHID migration are historical/future notes.
The former overview's “acceleration setting” is not in the current schema; the acceleration-curve editor is unimplemented. Middle-held low-sensitivity X/Y is Arduino history; Pico C.1 onward sends native Wheel/Pan.

Run commands at repository root unless stated otherwise.
[Development index](README.md) · [Project overview](../../README.md)

### Runtime settings and Flash storage

- **SET** immediately changes runtime RAM behavior, without Flash writes.
- **SAVE** explicitly persists current settings for reboot.
- **RESET** restores RAM defaults only; without SAVE, reboot reloads the previous saved values.
- **RESET → SAVE** persists defaults.
- **Missing/invalid storage** falls back to all defaults at boot.

The shared 44-byte v3 codec stores independent Pointer/Wheel settings, actions, and CRC32. Pico uses the existing `0x10FFF000–0x10FFFFFF` sector; Arduino uses EEPROM emulation. v1/v2 migrate in RAM, writing v3 only on the next explicit Save. Invalid records fall back to defaults. See [protocol](../../docs/protocol.md#flash保存形式と起動) and [C.2](../../firmware/redpoint_pico/MILESTONE_C2.md).
SET/RESET/boot never write Flash. SAVE skips erase/program when the stored record already matches.

Save success is reported only after commit and Flash readback comparison. The UI labels response-confirmed values **Saved**, changes **Unsaved changes**, and returning to that session's confirmed saved values restores Saved. Reconnect GET only reads RAM, so storage is shown as unconfirmed. No localStorage is used. SAVE failure/timeout is not considered Saved and can be retried. Old firmware may return NOT_IMPLEMENTED; persistence requires supporting firmware.

Flash operations briefly interrupt input/PS/2/HID processing. Save while stationary and keep USB connected until completion. After writing, discard partial frames/FIFO and resynchronize at the packet gap. A single sector is not power-loss atomic; interruption can lose prior settings, with invalid records falling back on reboot. Redundancy and wear leveling are unimplemented. Changing EEPROM layout/Flash size or performing a full erase may lose settings.

No settings/Serial data are sent externally or stored in analytics/localStorage. All assets are bundled; CSP permits same-origin API communication only. On Pages, GitHub receives ordinary static-page requests, not device data. Existing Pages workflow remains; this refactor performs no push/deploy or GitHub-setting changes.

### Communication and errors

[docs/protocol.md](../../docs/protocol.md) is the source of truth. GET/SET/RESET/SAVE framing is retained; action fields were added to config historically, followed by the current independent Pointer/Wheel schema.

- 115200 baud / 8N1 / no flow control. Distinguish `@CONFIG` and `@DEBUG`.
- Reassemble fragmented chunks into lines; debug/unknown/invalid JSON do not become configuration.
- Validate types/ranges and accept only the response corresponding to the current request.
- Debounce edits for 120 ms, coalesce latest values per field, and serialize requests.
- RESET discards unsent drafts and waits for any in-flight SET response.
- SAVE flushes pending SETs first; a SET failure cancels pending SAVE to avoid false persistence claims.
- A two-second timeout leaves mutation outcome unknown: discard unsent drafts and resync with newline + GET. Late SET/RESET/SAVE replies or errors cannot complete the resync GET. GET cannot prove a SAVE succeeded, so saving remains unconfirmed until an explicit retry. Failed resync disconnects and prompts reconnect.
- Disconnect aborts pending requests and releases stream locks before closing the port. Connect permits recovery after unplug/read/write failure.

`app.js` separates protocol, Serial transport, and UI functions. A future WebHID transport would require dedicated reports and firmware support; it is not currently implemented. See [Chrome's Web Serial API reference](https://developer.chrome.com/docs/capabilities/serial).

### Possible next phases

- Extended hardware operation/debug-load validation and UX refinement
- Power-loss-safe storage redundancy and endurance evaluation
- WebHID transport/firmware reports if needed

In-Configurator firmware updating, acceleration-curve editing, macros, multi-step chords, long press, double click, and layers are unimplemented.

### Onboard Device Status Indicator

The VCC-GND YD RP2040 WS2812 uses GPIO23, one GRB pixel at 800 kHz. **Bridge the RGB solder jumper.** It is separate from the monochrome LED. `STATUS_LED_BRIGHTNESS` in `status_led.h` is 8 of 255.
The historical Arduino environment uses **Adafruit NeoPixel 1.15.5**, required on other Arduino build setups, while retaining Philhower core 6.1.0 and the Pico SDK USB stack.

| Color | Meaning |
| --- | --- |
| WHITE | Boot, during the existing startup interval |
| BLUE | Normal |
| GREEN | Configurator activity within the last six seconds |
| YELLOW | Unsaved changes |
| PURPLE | Saving; LED display held for at least 150 ms |
| RED | Runtime error; two seconds from the latest error |

Boot overrides only during startup. Normal priority is **Error > Saving > Unsaved > Configurator active > Normal**.
Unsaved compares the complete logical config against the boot/last successful SAVE baseline (historically seven fields; currently ten). Returning to that baseline clears unsaved; RESET follows the same comparison. Failed SAVE does not change the baseline. Default fallback or migration alone does not trigger YELLOW/RED.

After GET, Configurator probes PING capability and sends an idle heartbeat every two seconds if supported. Disconnect/unplug stops the timer; LED activity expires up to six seconds after the last request. Unsaved remains YELLOW across reconnect. UNKNOWN_COMMAND from old compatible firmware disables only that session's heartbeat without a fatal UI message. See [protocol](../../docs/protocol.md) and [hardware checklist](../../docs/status-led-validation.md).

LED output occurs only on color changes from setup/main loop. Arduino's PIO-backed NeoPixel driver still has brief IRQ-masked intervals; host tests cannot guarantee no PS/2 impact. One-pixel wire transmission is about 30 µs, not the same as CPU interrupt-mask duration. During latch wait, Serial polling yields to the next loop while HID/PS/2 continue. Existing Flash IRQ masking and receive resynchronization are retained.
