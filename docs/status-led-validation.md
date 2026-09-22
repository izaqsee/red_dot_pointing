# Device Status Indicator 実機確認

自動テストは実際のLED電気信号、明るさ、PS/2 packet drop、HID stuckを検証しません。
この変更では実機upload／remote pushは実施していません。
人間がローカルfirmwareと新Configuratorを準備して以下を確認してください。
GitHub Pagesはこのローカル変更をpublishするまでは更新されません。

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

## 設計上の留意点

Adafruit NeoPixel 1.15.5はRP2040でPIOを使いますが、showの呼び出し側は割り込みを一時停止します。
1 pixelの3 bytesだけをFIFOへ渡し、同じ色は再送しません。実際の線上送信は約30 µsです。
ラッチ可能になるまでSerial pollを次loopへ譲ることで、SAVE直前のPURPLE送信を可能にし、
main loopをblocking delayしません。起動最初のshowだけはPS/2 IRQ初期化前です。
PIO／pixel buffer確保に失敗した場合はLED送信を停止し、既存入力・設定機能は継続します。

次phase候補: 実機でLED遷移時のPS/2受信統計を比較し、必要ならIRQ停止のないPIO専用driverを評価する。

## 自動検証結果（2026-09-22）

- `node --test tests/configurator.test.cjs`: 31/31 pass。
- `python tests/run_firmware_tests.py`: 既存record／action／persistenceテストとLED追加テストすべてpass。
- `arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint`: 成功。
  program 68,496 bytes（3%）、global RAM 10,604 bytes（4%）。
- `git diff --check`: 成功。

実機のLED色・操作感・packet drop・stuckについては上記手順での確認が残っています。
