# Device Status Indicator 実機確認

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

1. RGB solder jumperがbridgeされたYD RP2040を接続。WHITE → BLUEを確認。
2. 新ConfiguratorでConnect。GET同期／PING後GREENを確認。
3. pointerSensitivityまたはbutton actionを変更。YELLOWを確認。
4. 元の値へ戻すとGREEN。RESETでも最後の保存値との一致／不一致に従うことを確認。
5. 再変更してSAVE。PURPLE → GREEN、応答が表示時間で遅延しないことを確認。
6. 保存済み状態でDisconnect。最後の通信から最大6秒以内にBLUE。
7. 再接続・変更後、保存せずDisconnect。6秒を超えてもYELLOW。
8. 再接続してもYELLOW。SAVEでPURPLE → GREEN。
9. Serial terminalから無効コマンド／無効SETを送る。RED → 2秒後に本来の状態。
   2秒以内にもう一度エラーを送ると保持が延長されることを確認。
10. fresh Flash／invalid recordの正常fallback、valid v1 migration直後はBLUEであることを確認。
    Flashを変更する試験は設定バックアップのある試験用環境で行う。
11. TrackPointを連続操作しながら接続・SET・SAVE・エラーを繰り返す。
    pointer drop／stutterが実用上増えないことを比較確認。
12. mouse buttonとkeyboard shortcutを押しながら状態遷移し、解放後にbutton／key／modifierが残らないことを確認。
13. USB抜去・再接続で新sessionがGET → PING probeから開始することを確認。
14. PING非対応の旧firmwareにも接続し、UNKNOWN_COMMAND後のSET／RESET／SAVEが使え、PINGが繰り返されないことを確認。

### 設計上の留意点

Adafruit NeoPixel 1.15.5はRP2040でPIOを使いますが、showの呼び出し側は割り込みを一時停止します。
1 pixelの3 bytesだけをFIFOへ渡し、同じ色は再送しません。実際の線上送信は約30 µsです。
ラッチ可能になるまでSerial pollを次loopへ譲ることで、SAVE直前のPURPLE送信を可能にし、
main loopをblocking delayしません。起動最初のshowだけはPS/2 IRQ初期化前です。
PIO／pixel buffer確保に失敗した場合はLED送信を停止し、既存入力・設定機能は継続します。

次phase候補: 実機でLED遷移時のPS/2受信統計を比較し、必要ならIRQ停止のないPIO専用driverを評価する。

### 自動検証結果（2026-09-22）

- `node --test tests/configurator.test.cjs`: 31/31 pass。
- `python tests/run_firmware_tests.py`: 既存record／action／persistenceテストとLED追加テストすべてpass。
- `arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint`: 成功。
  program 68,496 bytes（3%）、global RAM 10,604 bytes（4%）。
- `git diff --check`: 成功。

実機のLED色・操作感・packet drop・stuckについては上記手順での確認が残っています。

## EN

1. Connect a YD RP2040 with the RGB solder jumper bridged. Check WHITE → BLUE.
2. Connect the Configurator. GET synchronization / PING should produce GREEN.
3. Change pointerSensitivity or a button action. Check YELLOW.
4. Restore the original value. Check GREEN. RESET also follows equality with the saved baseline.
5. Change a value and SAVE. Check PURPLE → GREEN without delaying the response for the display duration.
6. Disconnect with saved settings. BLUE should appear within six seconds of the last communication.
7. Reconnect, change a value, and disconnect without saving. YELLOW should persist past six seconds.
8. Reconnect: YELLOW remains. SAVE gives PURPLE → GREEN.
9. Send an invalid command/SET from a serial terminal. RED returns to the normal priority result after two seconds; another error within that interval extends it.
10. Check BLUE immediately after normal fresh-Flash/invalid-record fallback or valid v1 migration. Flash-corruption tests require a dedicated environment with settings backed up.
11. Move the TrackPoint continuously while connecting, SETting, SAVEing, and triggering errors. Compare pointer drop/stutter for practical regression.
12. Hold mouse buttons/keyboard shortcuts during transitions; verify no button, key, or modifier remains held after release.
13. Unplug/reconnect USB. A new session must begin with GET → PING capability probe.
14. With older firmware lacking PING, verify SET/RESET/SAVE still work after UNKNOWN_COMMAND and PING is not repeatedly sent.

### Design considerations

Adafruit NeoPixel 1.15.5 uses PIO on RP2040, but its show caller temporarily disables interrupts. Only three bytes for one pixel are queued; unchanged colors are not resent. Wire transmission takes about 30 µs.
Serial polling yields to the next loop until the LED latch is ready, permitting PURPLE immediately before SAVE without a blocking main-loop delay. Only the first boot show runs before PS/2 IRQ initialization. If PIO/pixel-buffer allocation fails, LED output stops while input/configuration continue.

Possible follow-up: compare PS/2 receive statistics during LED transitions and evaluate a dedicated PIO driver without IRQ masking if needed. This is the historical Arduino-driver note; the Pico backend is documented in the milestone reports.

### Automated results (2026-09-22)

- `node --test tests/configurator.test.cjs`: 31/31 passed.
- `python tests/run_firmware_tests.py`: all record/action/persistence and new LED tests passed.
- `arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint`: succeeded; 68,496 program bytes (3%), 10,604 global RAM bytes (4%).
- `git diff --check`: passed.

At that implementation stage, physical LED colors, feel, packet drops, and stuck inputs still required the checklist above. Later milestone hardware confirmations do not replace electrical/timing measurements not explicitly recorded.
