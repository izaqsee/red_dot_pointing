# Milestone B — Configurator over the frozen USB Ethernet architecture

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

> 現在の確認状況（2026-09-24）: C.2までの機能とC.3のiPad表示・touch操作は実機確認済みです。以下はMilestone B時点の履歴です。

A/Bはその後実機確認済みです。後続のhardware backendは[MILESTONE_C.md](MILESTONE_C.md)を参照してください。
Aで確認済みだったCDC-NCM/CDC Serial/HID Mouse/HID Keyboard、169.254.7.1/16、gateway/DHCPなし、Windows Code10なし/HTTP200、iPad Safari HTTPとWi-Fi併存をBでも維持しました。descriptor/endpoint/NCM/netifは変更なし。milestone_a_freeze.jsonは当時のdescriptor/HID/USB/frontend hashes、reference.jsonは外部TinyUSB由来の検査を保持します。product stringもfreezeのためAのままです。

### Static assetsとcommand経路

firmware/http_lwip/redpoint_http.cmakeのredpoint_attach_http(redpoint_reva)が既存tools/generate_http_fsdata.pyでbuild/redpoint-http/fsdata_redpoint.cを生成。CMakeは4 assetsとgeneratorを追跡し、標準fs.cがHTTPD_FSDATA_FILE経由でincludeします。独自HTTP serverは追加しません。`/`、`/index.html`、`/app.js`、`/shortcuts.js`、`/style.css`はcurrent configurator/*とbyte一致し、MIME/Content-Length/no-storeを付けます。placeholder src/status_fsdata.hは削除しました。

POST /api/command → 既存redpoint_httpd.cpp → executeConfigHttp() → firmware/redpoint/config_command.cpp内の単一executeConfigCommand()。firmware/直下に別command実装はありません。当時schema/framingは不変で、CDN/CORS/external backend/frontend変更もありません。
実lwIP host testで応答寿命の問題を発見しました。httpdはTCP ACKより先にcustom fileをcloseでき、fs_close_customがRAM応答をclearするとqueue/retransmitが解放済み領域を参照します。lwipopts.hからincludeするredpoint_http_opts.hでRAM custom responseだけTCP_WRITE_FLAG_COPYを使い、static/error pagesはzero-copyを維持します。他targetでもこのoptions headerが必要です。
既存adapterは96-byte body、単一command、Content-Type/Length/Origin、bounded slotsを検証。protocol errorはHTTP200のok:false、malformed HTTPは拒否します。

### Pico backendと対応command（B時点）

src/config_platform.cppはRAM-only backendです。bootでDEFAULT_CONFIGをloadしbaselineを設定。core0に1つのDeviceConfigだけを置き、HTTP/CDCで共有します。既存action validation/parser/formatterを変更せずbutton_action_codec.cppへ分離し、Arduinoもcompileします。config_record.cppのvalidatorを使いますがEEPROM driverはlinkしません。

| Command | B時点の動作 |
| --- | --- |
| GET | shared stateから有効な既存@CONFIG応答 |
| PING | 既存heartbeat応答、logical activity更新 |
| SET | 既存validation/mutation、RAMだけ、両transportへ反映 |
| RESET | DEFAULT_CONFIGへRAM reset、storage writeなし |
| SAVE | StorageError→SAVE_FAILED、成功を偽装しない |

SAVE失敗は設定/baseline/unsavedを維持。Flash writeはせずtakeConfigFlashWrite()は常にfalse、PS/2 resyncも仮実装しません。rebootはdefaultへ戻り、不正configはInvalidConfig。logical boot/activity/unsaved/errorはありますがWS2812出力/latchは未移植です。
src/config_cdc.cppでAのecho taskをTinyUSB stream adapterへ置換。Arduino Streamはlinkせず、bounded line assemblyとpartial TXだけを実装し同じcoreを呼びます。最大32 RX bytes/loop、TX FIFO満杯でも応答を保持し、送信完了後に次commandを読み、disconnectでpartial sessionをclear。別config/parserはありません。

### Build/test（2026-09-23）

READMEのconfigure後、repo rootで実行:

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_b.py
python firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
```

- ARM MinSizeRelのELF/UF2 link成功、warningなし。TinyUSB exampleのC専用warning flagsだけをC++から除き、意図的baseline float一致比較はsource単位で除外。
- Aの5群PASS。Bは生成text、最終ELF asset bytes、adapter/single core、frozen sourcesを追加検証。
- Visual Studio C++ Build Toolsによるnative統合で、ARM buildが選ぶ実lwIPとproduction Pico backend/HTTP/coreを実行。USBやlistenerは使わずsynthetic TCPで5 static paths、分割POST、GET/PING、shared SET/RESET、SAVE失敗、不正/oversize/重複/cross-origin headers、復帰、CDC partial TX/disconnect、volatile rebootを検証。
- native testが実GET/PINGをcapture。Node fixtureは変更前UIへ渡し、Serial/secure contextなしでsame-origin probe→GET→Connectedを検証。先に統合testと環境変数指定が必要、なければskip。最終55/55 PASS、failure/skipなし。
- 既存firmware host（actions/status/storage/HTTP）、fsdataもPASS。独立ARM compile-onlyもPASSですが既存float完全一致warningは残ります。

Host TCP testはsoftware処理の検証で、RP2040 timing/USB電気信号の検証ではありません。当該Milestoneの実装時点では実機検証未実施でした。
出力は`firmware/redpoint_pico/build/redpoint_reva.uf2`。
B時点のFlash/BIN 130,308 B（2 MiBの6.21%）、RAM 39,156 B（256 KiBの14.94%）＋Scratch Y stack 2,048 B、UF2 261,120 B。runtime heap/stack測定ではありません。

### B時点の実機確認手順

1. UF2を書き込む。
2. USB4機能とNCM Code10なしを確認。
3. iPad Safariで169.254.7.1を開き、permission/Serial/WebHIDなしで全UIが表示され、USB Ethernet · IPv4 Link-Local / Connectedになること、Wi-Fiも維持することを確認。
4. Windowsで5 static routes、GETのHTTP200/config frame、PINGをnetwork panelで確認。次でも確認可能:

   ```powershell
   curl.exe -i -H 'Content-Type: text/plain' --data-binary 'GET' http://169.254.7.1/api/command
   curl.exe -i -H 'Content-Type: text/plain' --data-binary 'PING' http://169.254.7.1/api/command
   ```

5. 設定変更後、DTRを有効にしたCDC terminalからLF/CRLFのGETで同じstateを確認。Bではまだphysical TrackPointへ反映せず、RESETはdefault、SAVEは必ず失敗。
6. 論理Disconnect/Connect、cable replug、rebootでdefaultへ戻ること、Wi-Fi併存、連続asset取得を確認。

### Cへ残した項目

本物のFlash保存/readback、USB/IRQ調整とpost-Flash TrackPoint resync、PS/2と3ボタン、RevA pins、WS2812、physical HID適用、runtime heap/stack/load測定。dummy保存/resyncを成功応答で隠していません。

### 変更ファイル

- Target: CMakeLists.txt、src/main.c、src/lwipopts.h、README.md。src/status_fsdata.h削除。config_platform.h/.cpp、config_cdc.cpp、milestone_a_freeze.json、本報告追加。
- Target tests: check_milestone_b.py、run_integration.py、native/integration.cpp、native/arch/cc.h、native/tusb.h、native/tusb_option.h。
- Shared: redpoint_http.cmake、新redpoint_http_opts.h、button_action.cppから新button_action_codec.cppへ分離。
- Regression: tests/run_firmware_tests.py、compile_http_lwip.py、configurator.test.cjs。

configurator/*、config_command.cpp、config_http.cpp、redpoint_httpd.cpp、USB descriptors、NCM、network設定はBでは不変です。

## EN

> Current verification status (2026-09-24): Functionality through C.2 and C.3 iPad display/touch operation have been verified on hardware. Unverified items, sizes and test results below describe the original implementation milestone.

Historical B report. A/B have since been hardware-verified. The current
target includes Milestone C; see [MILESTONE_C.md](MILESTONE_C.md).

Milestone A was confirmed on hardware: CDC-NCM + CDC Serial + HID
Mouse + HID Keyboard, 169.254.7.1/16, no gateway/DHCP, Windows without NCM Code 10,
Windows HTTP 200, iPad Safari HTTP and simultaneous Wi-Fi Internet. None of those
descriptors, endpoints, NCM sources or netif settings were changed for B.
`milestone_a_freeze.json` records the descriptor/HID/USB configuration and frontend
source hashes; `reference.json` retains the external TinyUSB provenance checks.
The USB product string remains the A string deliberately, as part of the freeze.

### Static assets and command path

`redpoint_attach_http(redpoint_reva)` from `firmware/http_lwip/redpoint_http.cmake`
generates `build/redpoint-http/fsdata_redpoint.c` with the existing
`tools/generate_http_fsdata.py`. CMake tracks all four input assets and the generator.
Standard lwIP `fs.c` includes it through `HTTPD_FSDATA_FILE`; no separate HTTP server
is added. `/`, `/index.html`, `/app.js`, `/shortcuts.js`, `/style.css` serve current
`configurator/*` byte-for-byte, including appropriate MIME, Content-Length and
no-store headers. The placeholder `src/status_fsdata.h` is deleted.

`POST /api/command` → existing `redpoint_httpd.cpp` → `executeConfigHttp()` →
the single `executeConfigCommand()` in `firmware/redpoint/config_command.cpp`.
The paths in this repo include `firmware/redpoint/`; there is no parallel command
implementation directly under `firmware/`. The schema and framing are unchanged.
No CDN, CORS, external backend or frontend modifications were added.

The real-lwIP host test exposed a reply-lifetime defect in the prior integration:
`httpd` can close a custom file after queuing it, before TCP ACKs arrive, while
`fs_close_custom` clears its RAM reply. `redpoint_http_opts.h`, included from
`lwipopts.h`, selects `TCP_WRITE_FLAG_COPY` only for RAM-backed custom responses.
Static assets/error pages remain zero-copy. This prevents queued/retransmitted
responses from referencing a cleared or reused request slot. Other targets using
the adapter must also include this options header from their `lwipopts.h`.

The existing adapter enforces the 96-byte request-body limit, single command,
Content-Type/Content-Length/Origin validation and bounded request slots. Protocol
errors remain HTTP 200 with `@CONFIG {"ok":false,...}`; malformed HTTP is rejected.

### Pico backend and supported commands

`src/config_platform.cpp` supplies a RAM-only backend. Boot loads DEFAULT_CONFIG
and establishes the same boot baseline used by the command core. There is exactly
one global DeviceConfig, owned on core 0 by both HTTP and CDC. The existing action
validation/parser/formatter is extracted unchanged into `button_action_codec.cpp`;
Arduino still compiles it alongside its physical action handling. `config_record.cpp`
provides the existing config validator; the EEPROM storage driver is not linked.

| Command | Milestone B status |
|---|---|
| GET | Real shared state, existing valid `@CONFIG` GET response |
| PING | Existing heartbeat response; updates logical activity |
| SET | Existing validation/mutation; RAM only, visible from both transports |
| RESET | Existing DEFAULT_CONFIG reset in RAM; no storage write |
| SAVE | Explicitly unsupported backend: `StorageError` → `SAVE_FAILED`; never success |

A failed SAVE preserves current settings and the baseline/unsaved flag. No Flash
write is attempted, `takeConfigFlashWrite()` is always false, and no PS/2 resync
is simulated. Reboot restores defaults. Invalid backend config returns
`InvalidConfig`. Logical boot/activity/unsaved/error state is available to the core;
there is no WS2812 hardware renderer or latch wait in B. This is not a completed
persistence or status LED port.

`src/config_cdc.cpp` replaces the A echo task with a TinyUSB stream adapter. The
Arduino `Stream` adapter is not linked; the TinyUSB adapter handles only bounded
line assembly and partial TX, and calls the same command core. It processes at
most 32 RX bytes per loop, retains responses across a full TX FIFO, drains each
response before reading another command, and clears partial sessions on disconnect.
It does not introduce another config state or another command parser.

### Build and tests (2026-09-23)

Use the configure command in README.md, then from the repo root:

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_b.py
python firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
```

- ARM MinSizeRel: final ELF/UF2 link succeeds without warnings. TinyUSB's C-only
  example warning flags are removed only from C++ sources; intentional exact
  baseline float comparisons have a per-source warning exemption.
- All five A build-check groups pass; B checks additionally verify generated text,
  bytes embedded in the linked ELF, linked adapters/single core and frozen sources.
- Native integration (Visual Studio C++ Build Tools): the actual lwIP sources
  selected by the ARM build run with the production Pico backend, HTTP adapter
  and core. Synthetic TCP packets exercise the real parser/server, without a USB
  device or host listener. All five static paths, split POST delivery, GET/PING,
  shared HTTP/CDC SET, RESET, failed SAVE, malformed/oversize/duplicate/cross-origin
  headers, recovery, CDC partial TX/disconnect and volatile reboot pass.
- The native integration captures real GET/PING bodies. The optional Node fixture
  test feeds those bodies into the unchanged UI and verifies same-origin probe →
  GET → Connected with no Serial API and no secure context. Run integration before
  Node with the environment variable above; otherwise that fixture test is skipped.
  Final run: **55/55 passed, zero failures, zero skipped**, including this fixture.
- Existing firmware host tests (actions, status, config storage and HTTP) pass.
- Existing fsdata test passes. The independent ARM compile-only HTTP/static check
  also passes; it retains its pre-existing exact-float-comparison warnings.

The host TCP test validates software processing, not RP2040 timing or physical USB.
At the Milestone B implementation stage, hardware validation had not yet been performed.

Output: `firmware/redpoint_pico/build/redpoint_reva.uf2`.
Flash/BIN: **130,308 B (6.21% of 2 MiB)**. RAM region: **39,156 B (14.94% of
256 KiB)** plus **2,048 B** Scratch Y stack reservation. UF2 container: **261,120 B**.
These are linker allocations, not runtime heap/stack measurements.

### Hardware confirmation

1. Install the UF2.
2. Confirm all four USB functions remain present, with no NCM Code 10.
3. On iPad Safari open `http://169.254.7.1/`. The full Configurator should load and
   become **Connected**, showing `USB Ethernet · IPv4 Link-Local`, without a device
   permission dialog, Web Serial or WebHID. Keep Wi-Fi Internet enabled.
4. On Windows inspect network requests: the five static routes return their correct
   assets, `POST /api/command` with `GET` returns HTTP 200 and a valid config frame,
   then heartbeat PING succeeds. The primary response is also checkable via:

   ```powershell
   curl.exe -i -H 'Content-Type: text/plain' --data-binary 'GET' http://169.254.7.1/api/command
   curl.exe -i -H 'Content-Type: text/plain' --data-binary 'PING' http://169.254.7.1/api/command
   ```

5. Change a setting, then query GET from a CDC terminal with DTR enabled and LF/CRLF
   framing. Confirm the same state in both transports. SET does not yet affect a
   physical TrackPoint. RESET returns defaults. SAVE must show failure, never Saved.
6. Test logical Disconnect/Connect, cable replug and reboot. Reboot resets RAM
   settings to defaults. Confirm Wi-Fi coexistence and sustained asset loads.

### Milestone C

Real EEPROM/Flash persistence with validation/readback, flash-safe USB/interrupt
coordination and actual post-flash TrackPoint resync; PS/2 TrackPoint and three
physical buttons; RevA peripheral pins and physical WS2812 rendering; physical HID
action application; runtime heap/stack/load measurements. No dummy persistence or
resync implementation is hidden behind a success response.

### Changed files

- Target: `CMakeLists.txt`, `src/main.c`, `src/lwipopts.h`, `README.md`; removed
  `src/status_fsdata.h`; added `src/config_platform.h`, `src/config_platform.cpp`,
  `src/config_cdc.cpp`, `milestone_a_freeze.json`, this report.
- Target tests: `tests/check_milestone_b.py`, `tests/run_integration.py`,
  `tests/native/integration.cpp`, `tests/native/arch/cc.h`, `tests/native/tusb.h`,
  `tests/native/tusb_option.h`.
- Shared: `firmware/http_lwip/redpoint_http.cmake`, new
  `firmware/http_lwip/redpoint_http_opts.h`; extraction from
  `firmware/redpoint/button_action.cpp` to new `button_action_codec.cpp`.
- Regression tests: `tests/run_firmware_tests.py`, `tests/compile_http_lwip.py`,
  `tests/configurator.test.cjs`.

`configurator/*`, `config_command.cpp`, `config_http.cpp`, `redpoint_httpd.cpp`,
USB descriptors, NCM implementation and network parameters are unchanged.
