# 開発・実機検証

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

現行のPico SDK版の検証入口は[C.3報告](../../firmware/redpoint_pico/MILESTONE_C3.md)です。
[A/B/C/C.1/C.2のregressionを含む検査](../../firmware/redpoint_pico/tests/check_milestone_c3.py)と
[HTTP・physical inputのhost統合テスト](../../firmware/redpoint_pico/tests/run_integration.py)を参照してください。
LEDの実機検証は[専用チェックリスト](../status-led-validation.md)を正とします。

以下はREADMEから移動した自動テスト説明と実機チェックリストです。
Normal / Middle感度やMiddle clickのbaselineはArduino/旧firmware向けです。
現行PicoではPointer/Wheelの独立設定、Middle保持によるWheel/Pan、v1/v2→v3移行として読み替え、
具体的な現行期待値は[C.2報告](../../firmware/redpoint_pico/MILESTONE_C2.md)を使ってください。
C.2まで実機確認済みですが、チェックリストは将来変更時の回帰確認用として残しています。
C.3のiPad実機での表示・touch操作も実機確認済みです。

すべてのコマンドは、特記がなければrepository rootで実行します。
[開発資料一覧](README.md) · [プロジェクト概要](../../README.md)

### 検証

Node.js 20以降があれば、外部依存なしで通信・状態遷移のテストを実行できます。
Nodeはテスト専用で、Configuratorの利用には不要です。

```powershell
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
```

firmware hostテストにはC++ compilerが必要です。WindowsではVisual Studio C++ Build Toolsを自動検出し、
Linux/macOSではc++ / g++ / clang++を使います。CXXでcompiler実行ファイルも指定できます。
本番の設定・保存・Actionコードをmock EEPROM / Mouse / Keyboardで実行し、実機へアクセスしません。
record破損、範囲外、CRC、default fallback、v1/v2移行、v3保存、SET/RESETの非永続性、
SAVEの検証・失敗・再読込・同値書込省略、Actionのlatch、重複所有、共有modifierのreleaseを確認します。
WebテストはRecorder、旧firmware互換、デバイス応答による確定、保存状態、timeout再同期も確認します。

テストはmockのSerial streamを使用します。実機では以下を確認してください。

1. 接続直後のGETで、ブラウザを開く前のデバイス設定が表示される。
2. Normal / Middleの感度変更とInvert X/Yが実際の移動に反映される。
3. sliderを連続操作しても、最終値がデバイス確認値と一致する。
   応答待ちに別項目や同じ項目を変更しても、最新操作値が消えない。
4. Middle押下、L/M/Rボタン、dragなどの既存HID動作が継続する。
5. pointer / button debugが大量に混在しても設定取得・変更できる。
6. RESET後にdefaultのデバイス確認値になり、実機の感度・反転も戻る。
7. Disconnect後のcontrol無効化、再接続、USB抜去中・送信中の切断から復帰できる。
8. ポート選択キャンセル、他アプリがCOM13を使用中、非対応ブラウザの表示を確認する。
9. SET→SAVE成功→電源再投入で保存値へ戻る。SETのみ→再起動では最後の保存値へ戻る。
10. RESETのみ→再起動で以前の保存値へ戻り、RESET→SAVE→再起動ならdefaultになる。
11. SAVE失敗・timeoutでSaved扱いにならず再試行できる。保存後の移動・Middle・3ボタンとdebugの復帰を確認する。
12. 未保存領域、不正magic/version/CRC/値のfixtureでdefault fallbackを確認する（通常利用機のFlashを不用意に壊さず、テスト機で行う）。
13. Pagesのproject URLからCSS/JSが読み込まれ、Connect・SET・SAVE・再接続できる。


### ボタン割当の実機確認

このphaseの自動テスト・buildはHID実機確認の代わりにはなりません。
新firmwareをbuild/uploadし、Mouse / Keyboard / Serialが同時に認識されることから確認してください。
既存v1移行の確認では、更新前にPointer設定をSaveし、全Flash消去を行わず更新します。

1. defaultのL/M/R click、drag、移動、小数移動、反転とMiddle中0.40倍がbaselineと同じ。
2. RightのRecord ShortcutでCtrl+Shift+Tを記録し、デバイス確認値の反映を待つ。
3. Rightの押下でshortcutが発火し、release後にCtrl/Shift/Tが残らない。
4. Save成功後にUSBを抜き差ししてbindingが保持される。
5. RightをMouse Middleにすると、Right押下中にMiddle感度になる。
6. MiddleをKeyboardへ割り当てると、Middle物理ボタンだけではMiddle感度にならない。
7. 2ボタンを同じshortcutにし、一方のreleaseで残るボタンのheld stateが消えない。Mouse Left重複も確認する。
8. Ctrl+CとCtrl+Shift+T等の共有modifierについて、release順を入れ替えてstuckしない。
9. ボタンを押したまま別ActionへSET／RESETし、その後releaseして旧Actionが残らない。次回DOWNから新Actionになる。
10. ResetでPointerとL/M/Rのdefault割当へ戻る。
11. Resetのみで再起動すると以前Saveしたbindingへ戻る。
12. Reset→Save→再起動でdefault割当になる。
13. 有効v1から起動するとPointer/invert設定を保持してdefault bindingsを追加する。Save後の再起動でも保持される。

併せてDisabled、Escape/Backspace、unsupported key、Cancel、記録中の切断、旧firmwareへの接続、
Pagesのproject URLでのRecorder、SAVE直後のPS/2再同期とdebugを確認してください。

## EN

Use the [C.3 report](../../firmware/redpoint_pico/MILESTONE_C3.md) as the current Pico SDK validation entry point, the [A/B/C/C.1/C.2 regression checker](../../firmware/redpoint_pico/tests/check_milestone_c3.py), and the [HTTP/physical-input host integration test](../../firmware/redpoint_pico/tests/run_integration.py). The [dedicated LED checklist](../status-led-validation.md) is authoritative for LED hardware validation.

The tests/checklists below were moved from the README. Normal/Middle sensitivity and Middle-click baseline references describe Arduino/older firmware. For current Pico firmware, use independent Pointer/Wheel settings, Middle-held Wheel/Pan, and v1/v2→v3 migration; exact expectations are in [C.2](../../firmware/redpoint_pico/MILESTONE_C2.md).
Features through C.2 and C.3 display/touch operation on a physical iPad are hardware-verified. Checklists remain for future regression testing.

Run commands from repository root unless stated otherwise.
[Development index](README.md) · [Project overview](../../README.md)

### Validation

Node.js 20+ runs communication/state-transition tests without external dependencies. Node is only needed for tests, not Configurator use.

```powershell
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
```

Firmware host tests need a C++ compiler. Windows automatically discovers Visual Studio C++ Build Tools; Linux/macOS use c++ / g++ / clang++. CXX can specify the executable.
Production config/storage/action code runs against mock EEPROM/Mouse/Keyboard, with no hardware access. Coverage includes record corruption/ranges/CRC/default fallback, v1/v2 migration and v3 save, volatile SET/RESET, SAVE validation/failure/readback/no-op writes, action latch/shared owners/modifier release. Web tests cover recorder, compatibility, authoritative replies, save state, and timeout resynchronization.

Serial streams are mocked. Hardware checklist:

1. Initial GET shows settings that existed before opening the browser.
2. Sensitivity and inversion affect movement (historical Normal/Middle; current Pointer/Wheel).
3. Continuous slider edits settle at the confirmed final value. Newer drafts survive edits while replies are pending.
4. Middle, L/M/R, dragging, and existing HID behavior continue.
5. Configuration still works amidst heavy pointer/button debug logging where enabled.
6. RESET returns displayed and physical sensitivity/inversion to defaults.
7. Disconnect disables controls; reconnect and USB removal during sends recover.
8. Check picker cancellation, a COM port held by another app (historically COM13), and unsupported-browser messages.
9. SET→SAVE→power cycle restores saved values; SET alone restores the previous saved values on reboot.
10. RESET alone preserves prior saved values; RESET→SAVE persists defaults.
11. Failed/timed-out SAVE must not show Saved; retry works. Movement/Middle/buttons/debug resume afterward.
12. Test fresh/invalid magic/version/CRC/value fixtures on a dedicated test device, not by corrupting a normal-use device.
13. Project Pages URLs load CSS/JS and support Connect/SET/SAVE/reconnect.

### Button hardware checks

Automated tests/builds do not replace HID hardware acceptance. After intentional upload, first confirm Mouse/Keyboard/Serial enumerate together. For v1 migration, save Pointer settings before updating without erasing all Flash.

1. Check default L/M/R click, drag, motion, fractional motion, inversion, and historical Middle 0.40× baseline; use native scroll expectations for current Pico.
2. Record Ctrl+Shift+T for Right and wait for device confirmation.
3. Right triggers the shortcut; release leaves no Ctrl/Shift/T held.
4. Save and replug; retain the binding.
5. Mapping Right to Mouse Middle enables Middle behavior while Right is held.
6. Mapping physical Middle to Keyboard removes Middle behavior from that button.
7. Map two buttons to the same shortcut, and also to Mouse Left: releasing one must not release the remaining owner's state.
8. Test shared modifiers (e.g. Ctrl+C and Ctrl+Shift+T) with both release orders; no stuck state.
9. SET/RESET while held must release the old action on UP and use the new action on next DOWN.
10. Reset restores Pointer and default L/M/R assignments.
11. Reset without Save then reboot restores the previously saved binding.
12. Reset→Save→reboot restores defaults.
13. Valid v1 preserves Pointer/inversion and adds default bindings; explicit Save and reboot preserve migration results.

Also check Disabled, Escape/Backspace, unsupported keys, Cancel, disconnect while recording, old-firmware compatibility, the recorder under project Pages, and PS/2 resync/debug immediately after SAVE.
