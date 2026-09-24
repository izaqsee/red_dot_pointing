# Milestone C — RevA hardware backend

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

> 現在の確認状況（2026-09-24）: C.2までの機能とC.3のiPad表示・touch操作は実機確認済みです。以下の未検証記述・サイズ・テスト結果は各Milestone実装時点の履歴です。

USB composite、descriptor、endpoint、current TinyUSB NCM、169.254.7.1/16、
gatewayなし／DHCPなし、device-hosted Configurator、HTTP adapter、command core、
frontendはA/Bの実機確認済み構成を維持。

### 移植元とbackend

| Arduino behavioral reference | Pico SDK側 | 維持する意味 |
|---|---|---|
| `redpoint.ino` GPIO／ISR／FIFO | `input_runtime.cpp`＋`platform_io.cpp` | CLK12 falling-edge、DATA13、odd parity、stop、128-entry FIFO、4 ms gap |
| packet parser／pointer計算 | `input_runtime.cpp` | sign一致のsanity check、USB X=dy/Y=dx、前後±127 clamp、invert、fractional accumulation |
| button GPIO／debounce | `input_runtime.cpp` | L3/M2/R4 pull-up、5 ms、down時latch、up時そのactionをrelease |
| `button_action.cpp` | 共通`button_action_state.cpp`＋各backend | Mouse/key/modifier owner count、重複割当、modifier前／key後の押下順 |
| Mouse／Keyboard APIs | `hid_state.cpp`＋`hid.c` | 既存Mouse/Keyboard interface、非同期TinyUSB reports |
| EEPROM／`config_storage.cpp` | Pico `config_storage.cpp`＋`flash_backend.cpp` | 明示SAVE、同一recordのwrite省略、readback、失敗時もresync |
| `config_record.cpp` | 同じsourceをlink | v2=36 B、CRC32、v1=24 B migration、default fallback |
| NeoPixel／`status_led.cpp` | 同じstate logic＋PIO backend | GPIO23、GRB、800 kHz、brightness 8、非blocking latch |

Arduino互換shimは追加していない。Arduino側の変更はaction owner処理を共通ファイルへ
移したことと、Mouse／Keyboard出力を薄いbackendに分離したこと。既存host testsで
Arduinoのaction API mappingとrelease semanticsも継続検証。

`config_command.cpp`、`config_http.cpp`、`config_record.cpp`は変更なし。同じ
`DeviceConfig config`をHTTP、CDC、物理入力が参照する。SET/RESETの効果はmain ownerが
`takeConfigChange()`をconsumeしてから物理入力へ適用。押下中のactionは再bindしない。
middleSensitivityはphysical middle pinではなくlogical Middle owner countに追従。
従来どおりwheelではなくautoscroll用の低感度relative X/Yとして送信する。

### Main loop／IRQ ownership

起動：board/GPIO → WHITE → Flash容量検査／record load／baseline → lwIP／TinyUSB →
startup-held action latch／PS2 IRQ enable → httpd → boot完了。

繰返しはcore 0だけで実行：

1. `tud_task()`（受信HTTP commandを含む）→ `sys_check_timeouts()`
2. `redpoint_config_cdc_task()`（32 RX bytes/loop、partial TX保持）
3. `redpoint_config_apply()`：`takeConfigChange()`でpointer/scrollの4 remainderをreset
4. 同じapply内で`takeConfigFlashWrite()`をconsumeし、FIFO/frame/packetをresync
5. button debounce/action更新 → PS/2 packet処理（Arduinoと同じくMiddle状態が先）
6. HID queue送信 → status LED update

command coreから入力stateを直接操作しない。GPIO IRQはbit capture、frame validation、
FIFOへの投入だけ。HTTP、Flash、HID、config mutationはIRQで実行しない。FIFO pop/reset
のみ短いIRQ maskを使う。PS/2 post-Flash resetはpending GPIO edgeもclearし、新しいgapを待つ。
SAVEでfractional remainder自体はresetしない（Arduinoと同じ）。

### HIDと意図的差分

- USB descriptorとreport descriptorはbyte単位で維持。旧neutral-report taskだけを
  physical report backendへ交換したため、Aのfreeze manifestから`hid.c/h`実装hashを
 外した。descriptor／USB設定／frontendのhashとdescriptor検査は維持。
- endpoint busy中はbounded queueでtransition順序を保持。各actionは1 HID key、
  3ボタン同時押下では最大3 distinct keysを既存6-key reportへ格納する。
- suspend／not-ready中もlogical owner stateは保持。移動履歴は再生せず、resume、
  mount／unmountの際にneutral→現在stateを送る。offline中に離したキーを再押下しない。
- queueが128件を超えた場合はneutral→現在stateに再同期する。無限buffer／USB待機を避ける
  意図的な過負荷時の挙動で、失われたreleaseによる恒久的stuckを防ぐ。
- Arduinoの起動時`delay(1000)`は使わず、startup-held actionを保持してenumeration後に
  送信する。PS/2処理は最大128 bytes/loopに制限しUSB/networkへ処理を返す。
- 高頻度`@DEBUG PTR/BTN`ログは追加しない。CDCは既存command adapterを維持する。
- HTTPはservice内でcommandを実行するため、CDCと合わせて入力処理の前にpending flagsを
  consumeする。入力処理中／IRQから設定変更はしない。

### Flash layoutと根拠

picotoolによる実測：**16384 KiB (16 MiB)**、
flash unique ID **0x500315198093931C**、B binary end **0x1001FD04**。
これはflash unique IDであり、RDID/JEDEC容量byteとは別物。

| 領域 | offset | XIP address |
|---|---|---|
| firmware＋embedded Configuratorに許可する範囲 | `0x00000000..0x00FFEFFF` | `0x10000000..0x10FFEFFF` |
| C実image（endはexclusive） | `0x00000000..0x000216C3` | `0x10000000..0x100216C3` |
| config専用4 KiB sector | `0x00FFF000..0x00FFFFFF` | `0x10FFF000..0x10FFFFFF` |
| v2 record（36 B） | sector先頭 | `0x10FFF000..0x10FFF023` |

プログラム時は先頭256 B pageへ36-byte record＋0xFF paddingを書き、sector残りは消去状態。
他用途とのsector共用はしない。Configurator assetsもfirmware linker region内なので保存と
衝突しない。UF2にはconfig sectorを含めない。通常の部分UF2更新はこのsectorを触らないが、
全消去は保存設定を消す。

ビルド側：SDK physical sizeを16 MiBに設定し、独立したlinker overrideでFLASH regionを
16,380 KiBに制限。`ASSERT(__flash_binary_end <= 0x10fff000)`、sector/page alignmentの
static_assert、ELF load segment／UF2全blockの検査で境界を確認。

実行側：boot時にIRQ停止下でRDID (`0x9f`)を読む。容量byteが24（2^24 bytes）であり、
manufacturer byteが0/FFではない場合のみstorageを有効化。それ以外はdefault load、
SAVE_FAILEDとなりerase/programしない。チップ容量を推測して書き込まない。

`flash_safe_execute()`でcore 0 IRQをmask。core 1は起動しない
（`PICO_FLASH_ASSUME_CORE1_SAFE=1`、ELFでcore1 launch不在を検査）。Flash callback、
SDK erase/programはSRAM配置、program pageもSRAM。SDK関数がROM処理とXIP復帰を管理する。
全256 B readback一致＋36 B record一致／CRC／値検証後だけSAVE成功を返す。
失敗経路でもwrite-attempt flagを立てて入力resyncする。書込み不要の同一record SAVEは
erase/programもresyncも不要。SET/RESET/bootはFlashを書かない。

Arduinoの**既定2 MiB設定**で保存した旧sectorは`0x101FF000`で、新sectorとは異なる。
旧addressからの推測importは行わない。移行前にGETで設定を控えて再設定／SAVEするか、
旧firmwareで16 MiB末尾sectorを使用していた場合はそのrecordをそのまま読み込む。
v1/v2の形式互換性は維持し、v1はRAM上でdefault actionsを補い次の明示SAVEでv2化する。

single-sector方式は元のEEPROM実装と同じく電源断atomicではない。消去／書込み中断後の
不正recordは次回bootでdefaultに戻す。二重sector journal等の新仕様は追加していない。

### WS2812

SDKの標準`pico_status_led/ws2812.pio`を読み込み、PIO0の空きSMをclaim。
GPIO23にGRB、800 kHzで24 bits送る。brightnessはNeoPixelと同じ
`channel * (8+1) >> 8`で最大8。送信30 µs＋reset latch300 µsの330 µs以内は
次の更新を保留し、CPUをsleep/busy-waitさせない。

WHITE boot、BLUE normal、GREEN activity（6秒）、YELLOW unsaved、PURPLE saving
（最低150 ms）、RED error（2秒）。boot後の優先順位はRED > PURPLE > YELLOW > GREEN > BLUE。
SAVE前にPURPLE wordをPIOへsubmitし、PIOはFlash処理中も自律送信。RED保持中はRED優先。
PINGの2秒heartbeatはfrontendを変更せず維持する。

### Build／test

READMEのconfigure commandへインストール済みpioasm指定を含めた。既存buildからは：

```powershell
cmake -S firmware/redpoint_pico -B firmware/redpoint_pico/build "-Dpioasm_DIR=$env:PIOASM_DIR"
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_c.py
python firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
```

検査対象：production PS/2 frame/FIFO/parity/stop/gap/sanity、axis/invert/sensitivity/
fractions/config reset、debounce/startup latch、disabled/mouse/key、shared refcount、
modifier overlap、USB busy/suspend、queue overflow後release、storage encode/CRC/v1/
reboot/no-op/readback failure/resync、LED state/priority/timers/latch。実lwIP TCP/httpd
を使った5 static paths、HTTP POST、HTTP/CDC→physical motion、SAVE成功／失敗、
malformed rejectionも含む。HALだけをhost mockにし、runtime/core/storage/state logicは
本番sourceを使用する。実flash driver／GPIO／PIOの電気的動作はARM buildでcompile/linkし、
実機での確認を別途行う。

最終linker使用量：Flash **136,900 B**、RAM **41,568 B**、Scratch Y stack予約 **4,096 B**。
stackはHTTP処理とSAVEを含むためA/Bの2 KiBから4 KiBへ増やした。
UF2 **273,920 B**：`build/redpoint_reva.uf2`。runtime high-water markではない。

実行結果：最終ARM build成功（warningなし）、Node **55/55 PASS／skipなし**、
既存firmware host test **4群PASS**、Pico native統合 **7群PASS**、A descriptor/build検査
**5群PASS**、B asset/core/freeze検査 **2群PASS**、C Flash/XIP検査 **2群PASS**、
fsdata unit test PASS、HTTP/static独立compile検査PASS。独立compile検査だけは既存の
意図的float完全一致比較に対するwarningを出す（本targetではsource単位で除外）。

### 実機確認手順／未検証事項

1. 必要な既存設定をGETで控える。16 MiB末尾以外に保存したArduino設定の自動importはない。
2. UF2を書き込んだ後、USB4機能・Windows NCM Code10なし・HTTP・iPad Wi-Fi併存を再確認。
3. WHITE→BLUE、Configurator ConnectedでGREEN、2秒heartbeatでGREEN維持、切断後6秒でBLUE。
4. TrackPoint X/Y方向、invert、感度0/0.5/1/高感度、低感度時の蓄積、logical Middleでの低感度を確認。
5. L/M/Rのdebounce、Mouse/key/disabled割当、同じactionの2ボタン同時押下、押下中のSETと
   元actionのrelease、modifier共有、suspend中release→resume後stuckなしを確認。
6. HTTP SET→実入力、CDC SET→同じ実入力、RESET後remainder resetを確認。
7. SAVE時PURPLE、成功応答／未保存解除、再起動後GETと実入力で復元を確認。
   同値SAVEはwrite不要。可能なら専用試験環境で失敗後RED／PS2復帰を確認。
8. SAVEしながらTrackPointを動かし、復帰後にpartial frame由来の異常移動が出ないことを確認。
9. HTTP asset load＋入力＋heartbeat＋SAVEの同時負荷、実Flash erase時間、stack/heap余裕、
   長時間動作、USB suspend消費電流を測定する。

当該Milestoneの実装時点では実機検証未実施でした。RDID応答、実erase/readback、
WS2812波形／配線、PS/2電気的timing、入力レイテンシ、電源断復旧と高負荷時性能は未検証。

### 変更ファイル

- 共通action engine：変更`firmware/redpoint/button_action.cpp`、追加
  `button_action_backend.h`／`button_action_state.cpp`。Arduino入力・storage・LED本体は変更なし。
- Pico追加：`src/input_runtime.{h,cpp}`、`hid_state.{h,cpp}`、`platform_io.{h,cpp}`、
  `config_storage.cpp`、`flash_backend.cpp`、`flash_layout.h.in`、`status_led.cpp`。
- Pico変更：`CMakeLists.txt`、`src/main.c`、`config_platform.{h,cpp}`、`hid.{h,c}`、
  `milestone_a_freeze.json`、`README.md`、履歴報告`MILESTONE_B.md`。追加：この報告。
- テスト：`tests/run_firmware_tests.py`、Picoの`tests/check_build.py`、
  `check_milestone_b.py`、新規`check_milestone_c.py`、`run_integration.py`、
  `native/integration.cpp`、新規`native/platform.cpp`／`hardware_tests.cpp`／`test_platform.h`。

変更しないもの：USB/report descriptors、endpoint、NCM、netif設定、HTTP adapter、
static generator、frontend、`config_command.cpp`、`config_http.cpp`、record codec。

## EN

> Current verification status (2026-09-24): Functionality through C.2 and C.3 iPad display/touch operation have been verified on hardware. Unverified items, sizes and test results below describe the original implementation milestone.

The hardware-verified A/B USB composite, descriptors, endpoints, current TinyUSB NCM, 169.254.7.1/16 without gateway/DHCP, device-hosted Configurator, HTTP adapter, command core and frontend were retained.

### Porting references and backends

| Arduino behavioral reference | Pico SDK implementation | Preserved semantics |
| --- | --- | --- |
| redpoint.ino GPIO/ISR/FIFO | input_runtime.cpp + platform_io.cpp | CLK12 falling edge, DATA13, odd parity, stop, 128-entry FIFO, 4 ms gap |
| Packet parser / pointer calculations | input_runtime.cpp | Sign sanity, USB X=dy/Y=dx, pre/post ±127 clamp, inversion, fractional accumulation |
| Button GPIO/debounce | input_runtime.cpp | L3/M2/R4 pull-ups, 5 ms, latch on press, release latched action |
| button_action.cpp | Shared button_action_state.cpp + backends | Mouse/key/modifier owners, shared mappings, modifier-before-key press order |
| Mouse/Keyboard APIs | hid_state.cpp + hid.c | Existing interfaces, asynchronous TinyUSB reports |
| EEPROM/config_storage.cpp | Pico config_storage.cpp + flash_backend.cpp | Explicit SAVE, identical-record no-op, readback, resync even on failure |
| config_record.cpp | Same source linked | v2=36 B, CRC32, v1=24 B migration, default fallback |
| NeoPixel/status_led.cpp | Same state logic + PIO backend | GPIO23, GRB, 800 kHz, brightness 8, nonblocking latch |

No Arduino compatibility shim was added. Arduino changes extracted action ownership into a shared file and Mouse/Keyboard output into thin backends. Existing host tests continue to verify Arduino action API mapping and releases. config_command.cpp, config_http.cpp and config_record.cpp were unchanged. HTTP, CDC and physical input share DeviceConfig config. The main owner consumes takeConfigChange() before applying SET/RESET to input; held actions are not rebound. At this milestone, middleSensitivity followed logical Middle owners rather than the physical middle pin and still sent low-sensitivity X/Y for OS autoscroll, not wheel reports.

### Main loop / IRQ ownership

Boot: board/GPIO → WHITE → Flash capacity check/record load/baseline → lwIP/TinyUSB → startup-held action latch/PS2 IRQ enable → httpd → boot complete.

Only core 0 runs the loop:

1. tud_task() (including incoming HTTP commands), then sys_check_timeouts().
2. redpoint_config_cdc_task(): 32 RX bytes per loop, preserving partial TX.
3. redpoint_config_apply(): consume takeConfigChange(), reset four pointer/scroll remainders.
4. In the same apply call, consume takeConfigFlashWrite() and resync FIFO/frame/packet state.
5. Button debounce/actions, then PS/2 packets: Middle state is updated first, as in Arduino.
6. Send HID queue, update status LED.

The command core does not directly alter input state. GPIO IRQ only captures bits, validates frames and enqueues FIFO data; no HTTP, Flash, HID or config mutation runs there. FIFO pop/reset briefly masks IRQs. Post-Flash PS/2 reset also clears pending GPIO edges and waits for a new gap. SAVE itself does not reset fractional remainders, matching Arduino.

### HID and intentional differences

- USB/report descriptors remain byte-identical. Replacing the neutral-report task with physical reports removed only hid.c/h implementation hashes from the A manifest; descriptor/USB/frontend hashes and descriptor checks remain.
- A bounded queue preserves transitions while endpoints are busy. Each action has one HID key; three physical buttons can produce up to three distinct keys in the existing six-key report.
- Suspend/not-ready preserves logical owners but discards motion history. Resume and mount/unmount send neutral then current state, without re-pressing keys released offline.
- Overflow beyond 128 queued events recovers to neutral/current state. This intentional overload behavior avoids unbounded storage, USB waits and permanently stuck releases.
- Arduino's delay(1000) is omitted. Startup-held actions wait for enumeration; PS/2 work is limited to 128 bytes per loop to service USB/network.
- No high-frequency @DEBUG PTR/BTN logs; CDC retains its command adapter.
- HTTP executes commands during service; HTTP/CDC flags are consumed before physical input. No config mutation occurs inside input processing or IRQ.

### Flash layout and rationale

picotool measurement: **16384 KiB (16 MiB)**, Flash unique ID **0x500315198093931C**, B binary end **0x1001FD04**. The unique ID is distinct from the RDID/JEDEC capacity byte.

| Region | Offset | XIP address |
| --- | --- | --- |
| Firmware + embedded Configurator permitted region | 0x00000000..0x00FFEFFF | 0x10000000..0x10FFEFFF |
| C image (end exclusive) | 0x00000000..0x000216C3 | 0x10000000..0x100216C3 |
| Dedicated 4 KiB config sector | 0x00FFF000..0x00FFFFFF | 0x10FFF000..0x10FFFFFF |
| v2 record (36 B) | Start of sector | 0x10FFF000..0x10FFF023 |

The first 256 B page contains the 36-byte record and 0xFF padding; the rest remains erased. The sector is not shared. Assets stay inside the firmware linker region. UF2 excludes the config sector; ordinary partial UF2 updates preserve it, while full erase removes settings.

The SDK physical size is 16 MiB; a separate linker override limits FLASH to 16,380 KiB. ASSERT(__flash_binary_end <= 0x10fff000), sector/page alignment static assertions and ELF segment/all-UF2-block checks enforce the boundary.

At boot, RDID (0x9f) is read with IRQs disabled. Storage is enabled only if capacity byte is 24 (2^24 bytes) and manufacturer is neither 0 nor FF. Otherwise defaults load and SAVE_FAILED is returned without erase/program. Capacity is never guessed.

flash_safe_execute() masks core 0 IRQs. Core 1 is never started (PICO_FLASH_ASSUME_CORE1_SAFE=1; ELF checked for absence of core1 launch). The Flash callback, SDK erase/program routines and program page reside in SRAM. SDK routines manage ROM operations and XIP restoration. SAVE succeeds only after full 256 B readback equality plus 36 B record/CRC/value validation. Failed attempts also set the resync flag. Identical-record SAVE needs neither erase/program nor resync. SET/RESET/boot never write Flash.

Arduino's **default 2 MiB** configuration used sector 0x101FF000, a different address. No guessed import is attempted: record GET values before migration and SET/SAVE them afterward. Records already stored at the 16 MiB end can be loaded directly. v1/v2 compatibility remains; v1 gains default actions in RAM and becomes v2 at the next explicit SAVE.

Like the original EEPROM implementation, this single-sector scheme is not power-loss atomic. Interrupted writes causing invalid records fall back to defaults next boot; no dual-sector journal was introduced.

### WS2812

The standard SDK pico_status_led/ws2812.pio claims a free PIO0 state machine. GPIO23 sends 24 bits, GRB, 800 kHz. Brightness matches NeoPixel: channel * (8+1) >> 8, maximum 8. Updates are deferred during the 30 µs transmission plus 300 µs reset latch (330 µs total), without CPU sleeping or busy-waiting.

WHITE boot, BLUE normal, GREEN activity (6 s), YELLOW unsaved, PURPLE saving (at least 150 ms), RED error (2 s). After boot: RED > PURPLE > YELLOW > GREEN > BLUE. PURPLE is submitted before Flash and PIO transmits autonomously during it; held RED takes priority. The frontend's 2-second PING heartbeat is unchanged.

### Build / tests

The README configure command now includes the installed pioasm path. From an existing build:

```powershell
cmake -S firmware/redpoint_pico -B firmware/redpoint_pico/build "-Dpioasm_DIR=$env:PIOASM_DIR"
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_c.py
python firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
```

Coverage includes production PS/2 frame/FIFO/parity/stop/gap/sanity, axis/inversion/sensitivity/fractions/config reset, debounce/startup latch, disabled/mouse/key actions, shared owners/modifiers, USB busy/suspend, overflow releases, storage encoding/CRC/v1/reboot/no-op/readback failure/resync, LED state/priority/timers/latch. Real lwIP TCP/httpd tests cover five static paths, HTTP POST, HTTP/CDC effects on physical motion, SAVE success/failure and malformed rejection. Only HAL is mocked; runtime/core/storage/state sources are production code. Real Flash/GPIO/PIO drivers are ARM compiled/linked; electrical behavior needs hardware validation.

Final linker allocation: Flash **136,900 B**, RAM **41,568 B**, Scratch Y stack **4,096 B** (increased from A/B's 2 KiB for HTTP and SAVE). UF2 **273,920 B**, build/redpoint_reva.uf2. These are not runtime high-water measurements.

Results: final ARM build passed without warnings; Node **55/55 PASS**, none skipped; existing firmware host tests **4 groups PASS**; Pico native integration **7 groups PASS**; A descriptor/build **5 groups PASS**; B assets/core/freeze **2 groups PASS**; C Flash/XIP **2 groups PASS**; fsdata unit test and independent HTTP/static compile passed. Only independent compilation warns about intentional exact float equality; the target suppresses this per source.

### Hardware acceptance / unverified at implementation time

1. Record required settings using GET; Arduino settings outside the 16 MiB end sector are not imported automatically.
2. After installing the UF2, recheck all four USB functions, Windows NCM without Code 10, HTTP and iPad Wi-Fi coexistence.
3. Check WHITE→BLUE, GREEN on Connected, retained by 2-second heartbeat, then BLUE 6 seconds after disconnect.
4. Check TrackPoint axes/inversion, sensitivity 0/0.5/1/high, low-sensitivity accumulation and logical Middle low sensitivity.
5. Check L/M/R debounce, mouse/key/disabled mappings, two owners of one action, SET while held followed by original-action release, shared modifiers and releases during suspend without stuck state after resume.
6. Check HTTP/CDC SET affects the same physical input and RESET clears remainders.
7. Check PURPLE on SAVE, success/clean state, reboot GET/input restoration and no write for identical SAVE. If possible in a dedicated setup, check RED and PS/2 recovery after failure.
8. Move TrackPoint during SAVE; check no abnormal motion from partial frames after recovery.
9. Measure concurrent assets/input/heartbeat/SAVE, real erase time, stack/heap margin, long-run operation and USB suspend current.

Hardware validation had not yet been performed at this milestone’s implementation stage. RDID, actual erase/readback, WS2812 waveform/wiring, PS/2 electrical timing, latency, power-loss recovery and heavy-load performance remained unverified at that time.

### Changed files

- Shared action engine: modified firmware/redpoint/button_action.cpp; added button_action_backend.h / button_action_state.cpp. Arduino input/storage/LED bodies unchanged.
- Pico additions: src/input_runtime.{h,cpp}, hid_state.{h,cpp}, platform_io.{h,cpp}, config_storage.cpp, flash_backend.cpp, flash_layout.h.in, status_led.cpp.
- Pico changes: CMakeLists.txt, src/main.c, config_platform.{h,cpp}, hid.{h,c}, milestone_a_freeze.json, README.md, historical MILESTONE_B.md; added this report.
- Tests: tests/run_firmware_tests.py; Pico tests/check_build.py, check_milestone_b.py, new check_milestone_c.py, run_integration.py, native/integration.cpp, new native/platform.cpp / hardware_tests.cpp / test_platform.h.

Unchanged: USB/report descriptors, endpoints, NCM, netif settings, HTTP adapter, static generator, frontend, config_command.cpp, config_http.cpp and record codec.
