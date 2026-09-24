# RedPoint

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

TrackPoint系モジュールを、独立したUSBポインティングデバイスとして使うための
ハードウェア・firmware・ブラウザConfiguratorの試作プロジェクトです。

<img src="docs/images/redpoint-revb.jpg" alt="RedPoint Rev.B prototype" width="600">

### Project status

**Working prototype / under development**

Pico SDK版はWindows/iPadで、ポインタ入力、nativeスクロール、ボタン割当、
USB Ethernet経由のConfigurator、設定保存・再起動後の復元まで実機確認済みです（C.2）。
コンパクトなUI（C.3）もbuild・自動テスト・ローカルブラウザ検証に加え、iPad実機での表示・touch操作まで確認済みです。

### 主な機能

- TrackPointによるUSB HID Mouse入力と、独立したLeft / Middle / Rightボタン
- logical Middleを保持して縦横スクロール（HID Wheel / Horizontal Pan）
- PointerとWheelで独立した感度・反転設定。Wheelは縦横の感度も個別に調整
- 各ボタンへのMouse action / Keyboard Shortcut / Disabled割当
- ブラウザからの設定変更と、明示的なSaveによるFlash保存
- WS2812による接続・未保存・保存中・エラーの状態表示

### Hardware

TrackPoint系PS/2モジュール、RP2040搭載ボード（VCC-GND YD RP2040等）、
3つの物理ボタン、レベル変換回路で構成します。上の写真はRev.B試作機です。
現在のUSB Ethernet firmware targetは[`firmware/redpoint_pico`](firmware/redpoint_pico/README.md)です。
対応pin・board条件を確認して使用してください。

### Configuratorで試す

**USB Ethernet版**ではRedPointをUSB接続し、[http://169.254.7.1/](http://169.254.7.1/)を開きます。
デバイスがConfiguratorを配信し、自動接続します。WindowsとiPadで確認済みで、iPadのWi-Fi Internetとも併用できます。

**Web Serial経由**では[公開Configurator](https://izaqsee.github.io/red_dot_pointing/)を
デスクトップ版Chrome / Edgeで開き、初回はConnectからRedPointへのアクセスを許可します。
公開ページのrevisionやfirmwareによる互換性は[運用資料](docs/development/configurator.md)を参照してください。

設定はすぐに動作へ反映されます。再起動後も使うには**Save**、RAM上の設定を初期値へ戻すには**Reset**を使います。

### Firmwareのbuild / upload

USB Ethernet版は[Pico SDK targetの手順](firmware/redpoint_pico/README.md)でbuildし、
生成した`redpoint_reva.uf2`を対応ボードのBOOTSELモードで書き込みます。
Arduino版は`firmware/redpoint`です。CLI / VS Code手順、必要な環境、COM自動検出の制限は
[Build / upload](docs/development/build-upload.md)にまとめています。

### Documentation

- [開発・保守資料一覧](docs/development/README.md)：build、Configurator運用、Pages公開、検証
- [Protocol / 設定・保存形式](docs/protocol.md)
- [USB Ethernet / lwIP構成](docs/usb-ethernet.md)
- [Status LEDの実機検証](docs/status-led-validation.md)

### License

RedPointのオリジナルソースコードは[MIT License](LICENSE)で公開しています。
第三者のソフトウェアに由来する部分には、それぞれのライセンスが適用されます。
[Third-party notices](THIRD_PARTY_NOTICES.md)を参照してください。

## EN

RedPoint is a prototype hardware, firmware, and browser Configurator project that turns a TrackPoint-style module into a standalone USB pointing device.

<img src="docs/images/redpoint-revb.jpg" alt="RedPoint Rev.B prototype" width="600">

### Project status

**Working prototype / under development**

The Pico SDK firmware has been hardware-verified on Windows and iPad through C.2: pointer input, native scrolling, button mapping, the USB Ethernet Configurator, and saved settings restored after reboot.
The compact C.3 UI has passed builds, automated tests, local browser checks, and display and touch verification on a physical iPad.

### Features

- TrackPoint input as a USB HID mouse, with separate Left / Middle / Right buttons
- Vertical and horizontal scrolling while holding a logical Middle action (HID Wheel / Horizontal Pan)
- Independent Pointer and Wheel sensitivity/inversion; separate horizontal and vertical Wheel sensitivity
- Per-button Mouse action / Keyboard Shortcut / Disabled assignments
- Browser-based configuration and explicit Save to Flash
- WS2812 indication of activity, unsaved settings, saving, and errors

### Hardware

A TrackPoint-style PS/2 module, an RP2040 board such as the VCC-GND YD RP2040, three physical buttons, and level-conversion circuitry. The photo shows the Rev.B prototype.
The USB Ethernet firmware target is [`firmware/redpoint_pico`](firmware/redpoint_pico/README.md). Check its pin assignments and board requirements before use.

### Try the Configurator

For **USB Ethernet firmware**, connect RedPoint over USB and open [http://169.254.7.1/](http://169.254.7.1/).
The device serves the Configurator and connects automatically. This has been verified on Windows and iPad, including simultaneous iPad Wi-Fi Internet access.

For **Web Serial**, open the [published Configurator](https://izaqsee.github.io/red_dot_pointing/) in desktop Chrome / Edge, then use Connect to grant initial device access.
See the [operation guide](docs/development/configurator.md) for compatibility and differences between published and local revisions.

Changes apply immediately. Use **Save** to retain them across reboot; **Reset** restores defaults in RAM.

### Build / upload firmware

Build the USB Ethernet version using the [Pico SDK target instructions](firmware/redpoint_pico/README.md), then copy the generated `redpoint_reva.uf2` to the supported board in BOOTSEL mode.
The Arduino target is `firmware/redpoint`. See [Build / upload](docs/development/build-upload.md) for CLI / VS Code setup and COM auto-detection limitations.

### Documentation

- [Development and maintenance](docs/development/README.md): builds, Configurator operation, Pages deployment, and validation
- [Protocol / configuration / storage format](docs/protocol.md)
- [USB Ethernet / lwIP architecture](docs/usb-ethernet.md)
- [Status LED hardware validation](docs/status-led-validation.md)

### License

RedPoint's original source code is available under the [MIT License](LICENSE).
Third-party portions retain their respective licenses. See [Third-party notices](THIRD_PARTY_NOTICES.md).
