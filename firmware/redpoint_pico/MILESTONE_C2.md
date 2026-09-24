# Milestone C.2 — Separate Pointer and Wheel configuration

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

> 現在の確認状況（2026-09-24）: C.2までの機能とC.3のiPad表示・touch操作は実機確認済みです。以下の未検証記述・サイズ・テスト結果は各Milestone実装時点の履歴です。

C.1 native scrollingはWindows/iPadで実機確認済み。C.2はローカルbuildとhost testまで完了。

### Canonical Config schema

| Group | Field | Default | Range |
| --- | --- | --- | --- |
| Pointer | pointerSensitivity | 1.00 | 0–10 |
| Pointer | pointerInvertX / pointerInvertY | false / false | bool (SETは0/1) |
| Wheel | wheelSensitivityX / wheelSensitivityY | 0.40 / 0.40 | 各0–10 |
| Wheel | wheelInvertX / wheelInvertY | false / false | bool (SETは0/1) |
| Buttons | leftAction / middleAction / rightAction | mouse:left / mouse:middle / mouse:right | 既存Action文字列 |

内部stateはこの10項目だけ。HTTP/CDCは同じconfig_command.cppを通る。

### Axis semantics

PS/2 decode/sanity/clampとlogical X = raw dy / logical Y = raw dxは不変。
logical Middle ownerなしではPointer感度・Invertだけを使いMouse X/Yを送信。
ownerありではWheel設定だけを使い、X→Pan、Y→Wheelへ送信する。
scroll中はMouse X/Y = 0、HID Middle press = 0。

base polarityはPan +logical X、Wheel -logical Y。
VerticalはC.1が逆だったという実機結果に基づき反転した。
Horizontalは期待どおりの方向であることが実機で確認されたため維持した。
Invert OFFをstick方向へviewが移動する基準とし、OS側Natural Scrollは対象外。
各Invertはそのgroup/axisだけを反転する。

Wheel X/Yそれぞれでlogical displacement × sensitivityを独立蓄積し、整数部のみ送信。
0でその軸を停止できる。小数部をcarryし、出力は[-127,127]へclamp。
飽和した整数の余剰分は破棄し、後から再生しない。PointerとWheelのfractionは別state。
mode切替、SET/RESETのmain-loop消費で全fractionをclearする既存方針を維持。
owner/refcount、押下時action latch、bounded HID queueと復帰動作は変更していない。

### Record v3 / migration

保存sectorは16 MiB Flash末尾 **0x10FFF000–0x10FFFFFF** のまま。
Flash backend、erase/program、readback検証、明示SAVE、post-Flash resyncは不変。

| Offset | v3 bytes | 内容 |
| --- | --- | --- |
| 0 | 4 | RPNT magic |
| 4 / 6 | 各2 | version 3 / length 44 |
| 8 / 12 | 各4 | pointerSensitivity / wheelSensitivityX (IEEE-754 LE) |
| 16 / 17 | 各1 | pointerInvertX / pointerInvertY |
| 18 | 2 | reserved zero |
| 20 / 24 / 28 | 各4 | Left/Middle/Right action: type, code, modifiers, reserved |
| 32 | 4 | wheelSensitivityY |
| 36 / 37 | 各1 | wheelInvertX / wheelInvertY |
| 38 | 2 | reserved zero |
| 40 | 4 | bytes 0–39の既存CRC32 |

v1は24 B、CRC offset20、actionsなし。v2は36 B、CRC offset32、actions offset20–31。
両方とも旧感度はoffset8/12、旧Invertは16/17。
旧pointerSensitivity/invertX/invertYを新Pointerへ保持し、旧middleSensitivityをWheel X/Yの両方へ移す。
Wheel Invertは両方false。v2 actionsを保持、v1にはdefault actionsを補う。
boot時はRAM移行だけで書かず、次の明示SAVEでv3へ更新する。
CRC/値/長さ/reservedなど不正なら全defaultへfallbackする。
旧firmwareへ戻した場合、その旧codecはv3を読めない（forward migrationのみ）。

### Protocol compatibility

完全削除やSET-only aliasでは旧PagesのGET schema検証が壊れるため、旧SET aliasと応答projectionを採用。

- `SET middleSensitivity v`: Wheel X/Y両方をvへ設定。
- `SET invertX/Y v`: Pointer Invert X/Yだけを設定。
- GET/SET/RESET/SAVE応答の追加互換field: middleSensitivity=wheelSensitivityY、invertX/Y=pointerInvertX/Y。
- PING/framing、error、timeout/resync、HTTP/CDC adapterは不変。

旧Pagesは新firmwareへ接続できるが、Wheel X/Yの独立差やWheel Invertを表示・編集できない。
旧middleSensitivity操作は両軸を同値に戻す。代表値だけで保存状態を判断する旧UIにはこの制限がある。
新UIは新schemaを要求するためC.1以前のfirmwareには旧Pagesを使う。

### Configurator

Pointer / Wheel / Buttonsの3カード。既存CSS/layout、Connection、status、shortcut recorderを維持。
変更したapp.jsはfield一覧、validConfig、Wheel fieldsetのenable/disableだけ。
createProtocol/createHeartbeat/HTTP-first/Serial fallback等は関数hashでC.1と一致確認。
app.js/index.html全体hashのfreezeだけをC.2のUI/schema変更に合わせて外し、
style.css/shortcuts.js全体hashとtransport/state-machine関数hashを維持する。
生成fsdataと最終ELF内のassetsはcurrent Configuratorとbyte一致。

### Changed files

- `configurator/app.js`, `configurator/index.html`
- `firmware/redpoint/config.h`, `config_command.cpp`, `config_record.cpp`, `config_record.h`
- `firmware/redpoint/redpoint.ino`: 共有schemaのfield参照追従のみ。Arduinoの歴史的Middle X/Y経路は移植対象外。
- `firmware/redpoint_pico/src/input_runtime.cpp`
- `firmware/redpoint_pico/milestone_a_freeze.json`, `milestone_c1_freeze.json` (new)
- `firmware/redpoint_pico/tests/check_milestone_b.py`, `check_milestone_c2.py` (new)
- `firmware/redpoint_pico/tests/native/hardware_tests.cpp`, `integration.cpp`, `scroll_tests.cpp`
- `tests/configurator.test.cjs`, `firmware_config_test.cpp`, `firmware_http_test.cpp`
- `README.md`, `docs/protocol.md`, `firmware/redpoint_pico/README.md`, `MILESTONE_C2.md` (new)

### Build / tests

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

ELF link / UF2生成成功、通常target buildにwarningなし。

| Check | Result |
| --- | --- |
| tests/run_firmware_tests.py | PASS: codec/CRC/v1+v2 migration、aliases、SAVE/readback/reboot、actions、LED、Serial/HTTP |
| firmware/redpoint_pico/tests/run_integration.py | 11群PASS: actual input/HID core、独立軸、C.1 queue regression、Pico storage migration、lwIP HTTP/static、CDC、persisted reboot |
| firmware/redpoint_pico/tests/check_milestone_c2.py | PASS: Aの5群、B/C assets/Flash検査、C.1 source/function freeze、PS/2 frame decoder |
| tests/http_fsdata_test.py | PASS |
| tests/compile_http_lwip.py build/compile_commands.json | PASS (compile-only) |
| node --test tests/configurator.test.cjs | 58/58 PASS、skip 0 |

NodeはREDPOINT_PICO_GET_RESPONSEにbuild/host-get-response.txtを指定し、host lwIPの実GET/PING応答でHTTP auto-connectも検証。
compile-onlyは既存の完全一致によるbaseline判定方針に対する-Wfloat-equalを3件出す（追加Wheel Yを含む）。

Flash **139,732 B / 16,380 KiB (0.83%)**。
RAM **41,840 B / 256 KiB (15.96%)**、別途Scratch Y main stack **4,096 B**。
linker allocationでありruntime high-water値ではない。
UF2 **279,552 B**:
`firmware/redpoint_pico/build/redpoint_reva.uf2`

### Hardware acceptance / unverified

1. 既存保存値を記録してからUF2を書き込む。HTTPでConnected、Pointer/Wheel/Buttons表示を確認。
2. v2のPointer/Actionを保持し、Wheel両軸が旧middleSensitivity、Wheel Invertが両方OFFであることをGETで確認。
3. OSの追加反転を考慮し、Middle保持で上下左右のstick方向へviewが動くか確認。横方向はC.1から変わらないこと。
4. Pointer InvertがWheelへ影響せず、Wheel InvertがPointerへ影響しないことを確認。
5. Wheel X/Yへ異なる感度を設定。各軸0でその軸だけ停止、低感度で小さい動きが蓄積されることを確認。
6. HTTP/CDC GET一致、各経路のSET即時反映、SAVE→再起動で独立値・Invert・actionsを復元することを確認。
7. RESET、複数Middle owner、mapping変更中release、suspend/resumeで不正押下やscroll履歴再生がないことを確認。
8. NCM/CDC/両HID、iPad Wi-Fi併存、status LED、SAVE失敗/再試行など既存動作も確認。

C.2の実機方向・migration・persisted rebootは未検証。Arduino targetの実機/buildは当該Milestoneの対象外。
UIイベント/markupは自動検証済みだが、ブラウザツールがfile URLを拒否したため表示の目視確認は未実施。

## EN

> Current verification status (2026-09-24): Functionality through C.2 and C.3 iPad display/touch operation have been verified on hardware. Unverified items, sizes and test results below describe the original implementation milestone.

C.1 native scrolling had already been hardware-verified on Windows/iPad. At implementation time, C.2 completed local builds and host tests.

### Canonical Config schema

| Group | Field | Default | Range |
| --- | --- | --- | --- |
| Pointer | pointerSensitivity | 1.00 | 0–10 |
| Pointer | pointerInvertX / pointerInvertY | false / false | bool (SET uses 0/1) |
| Wheel | wheelSensitivityX / wheelSensitivityY | 0.40 / 0.40 | 0–10 each |
| Wheel | wheelInvertX / wheelInvertY | false / false | bool (SET uses 0/1) |
| Buttons | leftAction / middleAction / rightAction | mouse:left / mouse:middle / mouse:right | Existing Action strings |

These ten fields are the only internal state. HTTP and CDC use the same config_command.cpp.

### Axis semantics

PS/2 decoding, sanity checks, clamping and logical X = raw dy / logical Y = raw dx are unchanged. Without a logical Middle owner, only Pointer sensitivity/inversion affects Mouse X/Y. With an owner, only Wheel settings apply: X → Pan, Y → Wheel. During scrolling, Mouse X/Y = 0 and HID Middle press = 0.

Base polarity is Pan +logical X, Wheel -logical Y. Vertical polarity was reversed based on hardware confirmation that C.1 was backwards. Horizontal polarity was retained after hardware verification confirmed it was correct. With Invert OFF, the view moves in the stick direction; OS Natural Scroll is outside this definition. Each inversion affects only its own group/axis.

Wheel X/Y independently accumulate logical displacement × sensitivity and send only the integral part. Zero disables that axis. Fractions carry; output clamps to [-127,127]. Saturated integer excess is discarded, never replayed. Pointer and Wheel fractions are separate. Mode transitions and main-loop consumption of SET/RESET clear all fractions as before. Owner/refcount, press-time action latching, bounded HID queues and recovery are unchanged.

### Record v3 / migration

The storage sector remains **0x10FFF000–0x10FFFFFF**, the last sector of 16 MiB Flash. Flash backend, erase/program, readback validation, explicit SAVE and post-Flash resync are unchanged.

| Offset | v3 bytes | Contents |
| --- | --- | --- |
| 0 | 4 | RPNT magic |
| 4 / 6 | 2 each | version 3 / length 44 |
| 8 / 12 | 4 each | pointerSensitivity / wheelSensitivityX (IEEE-754 LE) |
| 16 / 17 | 1 each | pointerInvertX / pointerInvertY |
| 18 | 2 | reserved zero |
| 20 / 24 / 28 | 4 each | Left/Middle/Right action: type, code, modifiers, reserved |
| 32 | 4 | wheelSensitivityY |
| 36 / 37 | 1 each | wheelInvertX / wheelInvertY |
| 38 | 2 | reserved zero |
| 40 | 4 | Existing CRC32 over bytes 0–39 |

v1 is 24 B, CRC offset 20, without actions. v2 is 36 B, CRC offset 32, actions at offsets 20–31. Both have old sensitivities at offsets 8/12 and inversion at 16/17. Old pointerSensitivity/invertX/invertY become Pointer settings; old middleSensitivity populates both Wheel axes. Both Wheel Invert values default to false. v2 actions are preserved; v1 receives default actions. Boot migrates in RAM only; the next explicit SAVE writes v3. Invalid CRC, values, length or reserved fields cause complete default fallback. Older firmware cannot decode v3: migration is forward-only.

### Protocol compatibility

Removing old fields or accepting SET-only aliases would break old Pages GET validation, so legacy SET aliases and response projections are provided.

- `SET middleSensitivity v`: set both Wheel sensitivities to v.
- `SET invertX/Y v`: set only Pointer Invert X/Y.
- GET/SET/RESET/SAVE add compatibility fields: middleSensitivity=wheelSensitivityY and invertX/Y=pointerInvertX/Y.
- PING, framing, errors, timeout/resync and HTTP/CDC adapters are unchanged.

Old Pages can connect to new firmware, but cannot display/edit independent Wheel sensitivities or Wheel Invert. Adjusting old middleSensitivity makes both axes equal. Its saved-state comparison sees only the representative value. The new UI requires the new schema; use old Pages with C.1 or earlier firmware.

### Configurator

Three cards: Pointer / Wheel / Buttons. Existing CSS/layout, Connection, status and shortcut recorder remain. app.js changes are limited to field lists, validConfig and Wheel fieldset enable/disable. Function hashes confirm createProtocol/createHeartbeat/HTTP-first/Serial fallback match C.1. Only whole-file app.js/index.html freezes were relaxed for the C.2 UI/schema changes; style.css/shortcuts.js and transport/state-machine function hashes remain frozen. Generated fsdata and assets in the final ELF match current Configurator bytes.

### Changed files

- `configurator/app.js`, `configurator/index.html`
- `firmware/redpoint/config.h`, `config_command.cpp`, `config_record.cpp`, `config_record.h`
- `firmware/redpoint/redpoint.ino`: shared schema references only; historical Arduino Middle X/Y behavior was outside this port.
- `firmware/redpoint_pico/src/input_runtime.cpp`
- `firmware/redpoint_pico/milestone_a_freeze.json`, `milestone_c1_freeze.json` (new)
- `firmware/redpoint_pico/tests/check_milestone_b.py`, `check_milestone_c2.py` (new)
- `firmware/redpoint_pico/tests/native/hardware_tests.cpp`, `integration.cpp`, `scroll_tests.cpp`
- `tests/configurator.test.cjs`, `firmware_config_test.cpp`, `firmware_http_test.cpp`
- `README.md`, `docs/protocol.md`, `firmware/redpoint_pico/README.md`, `MILESTONE_C2.md` (new)

### Build / tests

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

ELF linking and UF2 generation succeeded; normal target build had no warnings.

| Check | Result |
| --- | --- |
| tests/run_firmware_tests.py | PASS: codec/CRC/v1+v2 migration, aliases, SAVE/readback/reboot, actions, LED, Serial/HTTP |
| firmware/redpoint_pico/tests/run_integration.py | 11 groups PASS: actual input/HID core, independent axes, C.1 queue regression, Pico storage migration, lwIP HTTP/static, CDC, persisted reboot |
| firmware/redpoint_pico/tests/check_milestone_c2.py | PASS: A's 5 groups, B/C assets/Flash checks, C.1 source/function freeze, PS/2 frame decoder |
| tests/http_fsdata_test.py | PASS |
| tests/compile_http_lwip.py build/compile_commands.json | PASS (compile-only) |
| node --test tests/configurator.test.cjs | 58/58 PASS, 0 skipped |

Node used REDPOINT_PICO_GET_RESPONSE=build/host-get-response.txt to verify HTTP auto-connect with real host-lwIP GET/PING responses. Compile-only emitted three -Wfloat-equal warnings for intentional exact baseline comparison, including the added Wheel Y field.

Flash **139,732 B / 16,380 KiB (0.83%)**. RAM **41,840 B / 256 KiB (15.96%)**, plus Scratch Y main stack **4,096 B**. These are linker allocations, not runtime high-water marks. UF2 **279,552 B**: `firmware/redpoint_pico/build/redpoint_reva.uf2`.

### Hardware acceptance / unverified at implementation time

1. Record saved settings before installing the UF2. Verify HTTP Connected and Pointer/Wheel/Buttons.
2. GET must retain v2 Pointer/actions, set both Wheel sensitivities to old middleSensitivity and both Wheel Invert values OFF.
3. Accounting for OS inversion, hold Middle and check view movement follows the stick in all directions; horizontal behavior must match C.1.
4. Verify Pointer inversion does not affect Wheel and vice versa.
5. Set different Wheel sensitivities; zero disables only its axis, and small movements accumulate at low sensitivity.
6. Compare HTTP/CDC GET; check immediate SET effects and SAVE/reboot restoration of independent values, inversion and actions.
7. Check RESET, multiple Middle owners, releases during remapping, suspend/resume, no stuck presses or scroll replay.
8. Recheck NCM/CDC/both HIDs, iPad Wi-Fi coexistence, LED and failed SAVE/retry.

At implementation time, C.2 hardware direction, migration and persisted reboot remained unverified. Arduino build/hardware was outside scope. UI events/markup passed automated checks, but visual browser checks were blocked by file-URL rejection.
