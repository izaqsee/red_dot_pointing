# Milestone C.1 — Middle-held native HID scrolling

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

> 現在の確認状況（2026-09-24）: C.2までの機能とC.3のiPad表示・touch操作は実機確認済みです。以下はC.1実装時点の記録です。

### 動作と範囲

A/B/CはWindows/iPadで実機確認済みです。C.1はUSB/network/frontend/command core/Flash/LEDを維持し、Pico入力/HID backendだけを変更しました。Arduino版は歴史的な動作参照です。
logical Middleをscroll modifierとし、HID middle-button pressは送りません。PS/2 decode、logical X=raw dy/Y=raw dx、invertを適用後、Middle ownerが1つ以上ならY→Wheel、X→Horizontal Pan、Mouse X/Y=0。Middleなしのpointer動作は従来どおりでWheel/Pan=0。click passthroughは追加しません。
各軸でmovement×middleSensitivityを蓄積し、整数部を0方向へ切り捨てて±127へclamp、小数部だけcarryします。既存pointerと同様に、飽和した整数超過は後から再送しません。感度0はscrollなし。config変更消費とmode開始/終了で全fractionをclearし、移動のない切替も扱います。
共有action engineとlatch/releaseは不変で、どのphysical buttonもMiddleを所有できます。最後のowner解放までscroll継続し、ownerが残るhandoffではfractionを保持します。scroll中もLeft/Rightやkeyboard shortcutを使えます。
既存128件bounded mouse queueにwheel/panを追加。busy中は保持、offline/suspendは移動履歴を破棄、overflowはneutral→current復帰。GET_REPORT/current-stateのdeltaは0です。descriptor/endpointはbyte単位で不変。既存boot protocolはbuttons/X/Yのみなのでscrollには通常report protocolが必要で、boot時の擬似scrollは追加しません。

### 変更ファイル（firmware/redpoint_pico相対）

- src/input_runtime.cpp: scroll mode、fraction routing/reset。
- src/hid_state.cpp/.h: Wheel/Pan queue/API、Middle bit抑止。
- tests/native/platform.cpp、test_platform.h: mouseの5 fieldをcapture。
- tests/native/hardware_tests.cpp: Middle期待値更新。
- tests/native/scroll_tests.cpp: scroll/recovery追加検証。
- tests/native/integration.cpp、tests/run_integration.py: suite実行。
- README.md、MILESTONE_C1.md: 動作と検証報告。

### Buildと検証

既にconfigureしたtargetをbuildします。

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

ELF link/UF2生成成功、warningなし。Flash 137,020 B（16,380 KiBの0.82%）、RAM 41,824 B＋SCRATCH_Y stack 4,096 B。runtime high-waterではなくlinker allocationです。
UF2は`firmware/redpoint_pico/build/redpoint_reva.uf2`、274,432 B。config sector 0x10FFF000–0x10FFFFFFはELF/UF2外、保存形式とSAVE semanticsは不変。

```text
python firmware/redpoint_pico/tests/run_integration.py
python firmware/redpoint_pico/tests/check_milestone_c.py
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
node --test tests/configurator.test.cjs
```

Node実行時のREDPOINT_PICO_GET_RESPONSEは実lwIP統合testが生成したbuild/host-get-response.txtを指します。native統合10群（scroll、input、storage/LED、実HTTP/static、CDC/shared config）がPASS。Aの5群、Bのassets/core/freeze、CのFlash/SRAM/stackもPASS。既存firmware host、fsdata一致、HTTP compileもPASS。Nodeは55/55、skipなし。compile-onlyの未変更config_command.cppには既存float-equality warning 2件が残ります。

### C.1当時の実機確認項目

UF2を書き込んだ後、Windows/iPadで:

1. USB4機能、HTTP接続、Wi-Fi併存、通常pointer移動を確認。
2. 縦横scroll可能なページでMiddleを保持し、cursorが動かず両軸scrollし、OS autoscroll UIが出ないことを確認。
3. 感度0/0.5/1、蓄積、正負・斜め移動、logical axesに対するinvertX/Yを確認。
4. 各physical buttonをMiddleへmapし、2 ownerの片方release後もscroll、最後のrelease後はpointerへ戻ることを確認。
5. held中のmapping変更、latched release、mouse/keyboard併用、SET/RESETのfraction resetとstuck防止を確認。
6. suspend/resume、抜差し中の操作・releaseで履歴burstやstuckがないこと、SAVE/reboot persistenceも再確認。

当時はC.1の物理scroll、OS/applicationのPan対応・方向、高負荷USB timingが未検証でした。A/B/Cの確認だけでC.1成功とはせず、後日の実機確認を別途記録しています。

## EN

> Current verification status (2026-09-24): Functionality through C.2 and C.3 iPad display/touch operation have been verified on hardware. Unverified items, sizes and test results below describe the original implementation milestone.

### Behavior and scope

A/B/C were confirmed on Windows/iPad. C.1 retains the USB,
network, frontend, command core, Flash and LED architecture. Only the Pico
input/HID backend changes; the Arduino firmware remains the historical reference.

Logical Middle is a scroll modifier, never a HID middle-button press. Existing
PS/2 decode, axis transform (logical X = raw dy; logical Y = raw dx) and invert
settings run first. With at least one Middle owner, logical Y produces Wheel and
logical X produces Horizontal Pan; mouse X/Y are zero. Without Middle, pointer
behavior is unchanged and wheel/pan are zero. No middle-click passthrough is added.

Each scroll axis accumulates movement * middleSensitivity, truncates toward zero,
and retains its fractional remainder. Output is clamped to [-127,127]. As in the
existing pointer path, saturated integer excess is discarded, not replayed later;
the fraction survives. Sensitivity zero emits no scroll. Config-change consumption
and mode entry/exit reset all fractions, including transitions with no movement.

The shared action engine and latched release remain unchanged: any physical button
can own Middle, and only the last owner's release ends scrolling. Owner handoff
while Middle remains active preserves fractions. Mouse Left/Right and keyboard
shortcuts continue to work while scrolling.

The existing bounded 128-entry mouse queue now includes wheel/pan. Busy endpoints
retain events; offline/suspend discards transient history. Overflow retains the
existing neutral-then-current recovery. GET_REPORT/current-state reports contain
zero deltas. USB mouse/report descriptors and endpoints are byte-for-byte frozen.
The pre-existing HID boot protocol sends only buttons/X/Y; scrolling requires the
normal report protocol. No descriptor redesign or boot-protocol emulation is added.

### Changed files (relative to firmware/redpoint_pico)

- src/input_runtime.cpp: logical scroll mode and fractional motion routing/reset.
- src/hid_state.cpp and src/hid_state.h: wheel/pan queue/API; suppress Middle bit.
- tests/native/platform.cpp and test_platform.h: capture all five mouse fields.
- tests/native/hardware_tests.cpp: update prior Middle expectations.
- tests/native/scroll_tests.cpp: new scroll behavior/recovery coverage.
- tests/native/integration.cpp and tests/run_integration.py: run scroll suite.
- README.md and MILESTONE_C1.md: current behavior and validation report.

### Build and validation

Build the existing configured target:

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

Successful final ELF link and UF2 generation; no build warnings.
Flash: 137,020 B (0.82% of 16,380 KiB firmware region).
RAM: 41,824 B, plus 4,096 B main stack in SCRATCH_Y.
These are linker allocations, not runtime high-water measurements.
UF2: `firmware/redpoint_pico/build/redpoint_reva.uf2`
(274,432 B). Flash config sector remains 0x10FFF000–0x10FFFFFF,
excluded from ELF/UF2; storage format and SAVE semantics unchanged.

Validation commands:

```text
python firmware/redpoint_pico/tests/run_integration.py
python firmware/redpoint_pico/tests/check_milestone_c.py
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
node --test tests/configurator.test.cjs
```

For Node tests, REDPOINT_PICO_GET_RESPONSE points to build/host-get-response.txt
captured by the real lwIP host integration test. Native integration passes ten
groups including scroll, physical input, storage/LED, actual HTTP/static delivery,
CDC and shared config effects. A's five descriptor/build groups, B's asset/core/
freeze checks and C's Flash/SRAM/stack checks pass. Existing firmware host tests,
fsdata equality and HTTP compile checks pass. Configurator Node tests pass 55/55 (zero skipped). The compile-only check retains two
pre-existing float-equality warnings in unchanged config_command.cpp.

### Hardware verification outstanding at the C.1 implementation stage

After installing the UF2, verify on Windows and iPad:

1. All four USB functions enumerate; Configurator connects over HTTP and Wi-Fi
   coexistence is unchanged. Normal TrackPoint motion moves the pointer.
2. Hold logical Middle over a vertically/horizontally scrollable page: both axes
   scroll, the pointer stays stationary, and no OS middle-autoscroll UI appears.
3. Set middleSensitivity to 0, 0.5 and 1; verify zero, fine accumulated motion,
   both signs, diagonal motion and invertX/invertY against the logical axes.
4. Map each physical button to Middle. Map two simultaneously, release one,
   confirm continued scrolling, then release the last and confirm pointer motion.
5. Change mappings while held; verify latched release, mouse/keyboard combinations
   and no stuck action. SET/RESET must not carry old scroll fractions.
6. Suspend/resume and unplug/replug while moving/holding/releasing; confirm no
   backlog burst and no stuck buttons/keys. Recheck SAVE/reboot persistence.

C.1 physical scrolling, OS/application horizontal-pan support/direction, and actual
USB timing under sustained traffic were unverified at implementation time. Prior A/B/C hardware confirmation alone did not establish C.1 success; subsequent hardware verification is recorded separately.
