# Milestone C.2 — Separate Pointer and Wheel configuration

C.1 native scrollingはWindows/iPadでユーザー実機確認済み。C.2はローカルbuildとhost testまで完了。
remote push・hardware uploadは行っていない。

## Canonical Config schema

| Group | Field | Default | Range |
| --- | --- | --- | --- |
| Pointer | pointerSensitivity | 1.00 | 0–10 |
| Pointer | pointerInvertX / pointerInvertY | false / false | bool (SETは0/1) |
| Wheel | wheelSensitivityX / wheelSensitivityY | 0.40 / 0.40 | 各0–10 |
| Wheel | wheelInvertX / wheelInvertY | false / false | bool (SETは0/1) |
| Buttons | leftAction / middleAction / rightAction | mouse:left / mouse:middle / mouse:right | 既存Action文字列 |

内部stateはこの10項目だけ。HTTP/CDCは同じconfig_command.cppを通る。

## Axis semantics

PS/2 decode/sanity/clampとlogical X = raw dy / logical Y = raw dxは不変。
logical Middle ownerなしではPointer感度・Invertだけを使いMouse X/Yを送信。
ownerありではWheel設定だけを使い、X→Pan、Y→Wheelへ送信する。
scroll中はMouse X/Y = 0、HID Middle press = 0。

base polarityはPan +logical X、Wheel -logical Y。
VerticalはC.1が逆だったという実機結果に基づき反転した。
Horizontalはユーザーの「期待どおりだった」という追加確認に基づき維持した。
Invert OFFをstick方向へviewが移動する基準とし、OS側Natural Scrollは対象外。
各Invertはそのgroup/axisだけを反転する。

Wheel X/Yそれぞれでlogical displacement × sensitivityを独立蓄積し、整数部のみ送信。
0でその軸を停止できる。小数部をcarryし、出力は[-127,127]へclamp。
飽和した整数の余剰分は破棄し、後から再生しない。PointerとWheelのfractionは別state。
mode切替、SET/RESETのmain-loop消費で全fractionをclearする既存方針を維持。
owner/refcount、押下時action latch、bounded HID queueと復帰動作は変更していない。

## Record v3 / migration

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

## Protocol compatibility

完全削除やSET-only aliasでは旧PagesのGET schema検証が壊れるため、旧SET aliasと応答projectionを採用。

- `SET middleSensitivity v`: Wheel X/Y両方をvへ設定。
- `SET invertX/Y v`: Pointer Invert X/Yだけを設定。
- GET/SET/RESET/SAVE応答の追加互換field: middleSensitivity=wheelSensitivityY、invertX/Y=pointerInvertX/Y。
- PING/framing、error、timeout/resync、HTTP/CDC adapterは不変。

旧Pagesは新firmwareへ接続できるが、Wheel X/Yの独立差やWheel Invertを表示・編集できない。
旧middleSensitivity操作は両軸を同値に戻す。代表値だけで保存状態を判断する旧UIにはこの制限がある。
新UIは新schemaを要求するためC.1以前のfirmwareには旧Pagesを使う。

## Configurator

Pointer / Wheel / Buttonsの3カード。既存CSS/layout、Connection、status、shortcut recorderを維持。
変更したapp.jsはfield一覧、validConfig、Wheel fieldsetのenable/disableだけ。
createProtocol/createHeartbeat/HTTP-first/Serial fallback等は関数hashでC.1と一致確認。
app.js/index.html全体hashのfreezeだけを今回の許可範囲に合わせて外し、
style.css/shortcuts.js全体hashとtransport/state-machine関数hashを維持する。
生成fsdataと最終ELF内のassetsはcurrent Configuratorとbyte一致。

## Changed files

- `configurator/app.js`, `configurator/index.html`
- `firmware/redpoint/config.h`, `config_command.cpp`, `config_record.cpp`, `config_record.h`
- `firmware/redpoint/redpoint.ino`: 共有schemaのfield参照追従のみ。Arduinoの歴史的Middle X/Y経路は移植対象外。
- `firmware/redpoint_pico/src/input_runtime.cpp`
- `firmware/redpoint_pico/milestone_a_freeze.json`, `milestone_c1_freeze.json` (new)
- `firmware/redpoint_pico/tests/check_milestone_b.py`, `check_milestone_c2.py` (new)
- `firmware/redpoint_pico/tests/native/hardware_tests.cpp`, `integration.cpp`, `scroll_tests.cpp`
- `tests/configurator.test.cjs`, `firmware_config_test.cpp`, `firmware_http_test.cpp`
- `README.md`, `docs/protocol.md`, `firmware/redpoint_pico/README.md`, `MILESTONE_C2.md` (new)

## Build / tests

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
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
`E:/projects/red_dot_pinting/firmware/redpoint_pico/build/redpoint_reva.uf2`

## Hardware acceptance / unverified

1. 既存保存値を記録してからユーザーがUF2をupload。HTTPでConnected、Pointer/Wheel/Buttons表示を確認。
2. v2のPointer/Actionを保持し、Wheel両軸が旧middleSensitivity、Wheel Invertが両方OFFであることをGETで確認。
3. OSの追加反転を考慮し、Middle保持で上下左右のstick方向へviewが動くか確認。横方向はC.1から変わらないこと。
4. Pointer InvertがWheelへ影響せず、Wheel InvertがPointerへ影響しないことを確認。
5. Wheel X/Yへ異なる感度を設定。各軸0でその軸だけ停止、低感度で小さい動きが蓄積されることを確認。
6. HTTP/CDC GET一致、各経路のSET即時反映、SAVE→再起動で独立値・Invert・actionsを復元することを確認。
7. RESET、複数Middle owner、mapping変更中release、suspend/resumeで不正押下やscroll履歴再生がないことを確認。
8. NCM/CDC/両HID、iPad Wi-Fi併存、status LED、SAVE失敗/再試行など既存動作も確認。

C.2の実機方向・migration・persisted rebootは未検証。Arduino targetの実機/buildは今回対象外。
UIイベント/markupは自動検証済みだが、ブラウザツールがfile URLを拒否したため表示の目視確認は未実施。
