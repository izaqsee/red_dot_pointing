# USB Ethernet Configurator / lwIP integration

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

### 現在のtarget

統合済みPico SDK targetは[firmware/redpoint_pico](../firmware/redpoint_pico/README.md)です。
USB Ethernet、HID、Flash、LED、Pointer/Wheel独立設定はC.2までWindows/iPadで実機確認済みです。
C.3のiPad表示・touch操作もユーザー確認済みです。各段階の結果は[C.2](../firmware/redpoint_pico/MILESTONE_C2.md)と[C.3](../firmware/redpoint_pico/MILESTONE_C3.md)を参照してください。以下のAPI/lwIP詳細は引き続き構成の参照資料です。

### 初期adapter段階の範囲（履歴）

この節は統合Pico target以前のcompile-only段階を記録したもので、現在の実装状況ではありません。
frontendはsame-origin HTTPを優先し、既存Web Serialへfallbackします。`firmware/http_lwip`は独自TCP/HTTP serverではなく、**標準lwIP httpdの拡張**です。両adapterは単一の`config_command.cpp`を使います。
Arduino sketchはCDC + Mouse + Keyboardをbuildしますが、それだけでEthernetは有効になりません。当時の実機確認済みnetwork実験は別Pico SDK/TinyUSB project `E:/projects/tinyusb-master/examples/device/net_lwip_webserver`でした。

- `main.c`: IP 169.254.7.1、mask 255.255.0.0、gateway 0.0.0.0。
- `lwipopts.h`: NO_SYS=1、DHCPなし、標準httpd/fs。
- TinyUSB network callbackはmain taskからlwIPへ渡します。
- USB descriptors/platform初期化はArduino HID sketchとは別。外部checkoutは変更していません。

HTTP adapter/core/生成assetsを、その実験のARM compiler/includeと実際のhttpd.c/fs.cでcompileしました。当時の結果は**compile確認であり、統合Ethernet+HIDのlink/実機確認ではありません**。

### FrontendとAPI

起動時、相対URL `api/command`へHTTP POSTでGET **protocol command**を送ります。有効なframing/config応答ならHTTPを選び、その応答で初期同期します。Serial APIもsecure contextも不要です。失敗時はHTTP sessionをclose/abortし、既存Serial探索・permission処理へ移ります。GitHub Pagesもfallback経由で使用できます。

```http
POST /api/command HTTP/1.1
Content-Type: text/plain
Cache-Control: no-store
Content-Length: 4

GET
```

応答headerは`Content-Type: text/plain; charset=utf-8`と`Cache-Control: no-store`です。

```text
@CONFIG {"ok":true,"command":"GET","config":{...}}
```

- GET/SET/RESET/SAVE/PINGは既存parser/state machineを使用。
- protocol errorはHTTP 200と`@CONFIG {"ok":false,...}`で返す。
- request body上限は改行込み96 bytes、1 commandのみ。GET resync用の前後CR/LFは許可。空、NUL/control/non-ASCII、複数command、長すぎるcommandは実行前に拒否。framing/size不正は400/413。
- HTTP GET `/api/command`は405。未対応URI/content type、重複content header、Transfer-Encodingは拒否。Content-Length必須。
- CORSなし。Originがあれば`http://`＋Hostと一致必須で、cross-origin browser書込みを拒否。ローカル・認証なしAPIでありInternet公開用ではなく、HTTPSも実装しない。
- 最大4 POST contextは独立固定bufferを持ち、送信中の応答を共有global bufferで上書きしない。中断bodyは破棄。完了bodyはhttpd_post_finishedで一度だけ実行し、fs_close_customで応答領域を解放。context満杯/LED latch待ちは503。
- Disconnectはfetch中断・heartbeat停止・session破棄という論理切断。ConnectはGET同期から再開し、Ethernet interface自体は閉じない。
- fetchごとにabort controller、deadline、generationを持つ。abortが無視されても古い応答で新しいresync GETを完了させない。
- 既存2秒protocol timeoutとresync queueを優先。fetch/status failureはtransport error/disconnect経路へ流す。

frontendにdevice IPを固定せず、page originからAPI hostを決めます。CSPは`connect-src 'self'`で、外部serviceを追加しません。

### Network firmware targetへの組込み

**RedPoint統合network target**を定義するCMakeから呼びます。

```cmake
include("/path/to/red_dot_pinting/firmware/http_lwip/redpoint_http.cmake")
redpoint_attach_http(your_target)
```

targetは標準lwIP `src/apps/http/httpd.c`と`fs.c`を既にcompileする必要があります。helperはPOST/custom filesを有効にし、config_command.cpp/config_http.cpp/redpoint_httpd.cppを追加し、Python標準libraryでfsdataを生成します。fsdataはfs.cがHTTPD_FSDATA_FILE経由でincludeするため、別sourceとしてcompileしないでください。
既に全RedPoint sourceを含むtargetではconfig_command.cppを二重追加せず、source listを明示してください。

必要な実platform API（stubではないもの）:

- loadDeviceConfig/saveDeviceConfig/takeConfigFlashWrite。当初はrecord v2/v1 migration、現在の共通codecはv3とv1/v2読込み。Arduino config_storage.cppはEEPROMを使用。
- Status LED API。当初のstatus_led.cppはArduino/Adafruit NeoPixel用。
- Button action validation/formattingとHID backend。当初はbutton_action.cpp内。
- CDCを残す場合のSerial adapter（Arduino config.cpp/config_serial.h、Picoには専用adapter）。

network-only exampleにはこれらがないため、command coreを追加するだけではlinkできません。helperはfake SAVE success、dummy LED、別RAM-only config backendを提供しません。
boot時に実LED/storageとbaselineを初期化し、netif初期化後に標準httpd_init()を呼びます。実機確認済みIP/mask、gatewayなし、DHCPなしをnetif側で維持します。
command実行はIRQや並行TCP/IP threadではなく、**1つのmain-loop owner**で行います。NO_SYS taskも同じloopからserviceする必要があります。Serial/network処理後:

1. takeConfigChange()をconsumeし、SET/RESETのpointer/scroll remainderをclear。
2. takeConfigFlashWrite()をconsumeし、失敗時も含めPS/2 FIFO/frameをpost-Flash resync。
3. statusLedUpdate()と通常HID/PS/2処理を継続。

Arduino loopも共有change flagをconsumeするようになり、将来のHTTP変更でもaccumulator resetを通る構成です。

### Static files

tools/generate_http_fsdata.pyは現在のConfiguratorを通常のstruct fsdata_fileへ埋込み、MIME、Content-Length、no-store headerを付けます。minify、CDN、server-side template、別frontend copyは使いません。

```powershell
python tools/generate_http_fsdata.py --output build/http/fsdata_redpoint.c
```

生成routeは`/`、`/index.html`、`/app.js`、`/shortcuts.js`、`/style.css`。`/api/command`はPOST callbackで動的処理。filesはFlash内に置き、asset変更時に再生成します。

### 初期段階の検証・実機への引継ぎ（履歴）

```powershell
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py E:/projects/tinyusb-master/examples/device/net_lwip_webserver/build/compile_commands.json
arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint
```

Host testはSerial/HTTP応答一致、mutation、不正/複数body、分割pbuf、中断cleanup、pool枯渇、header、asset byte一致を検証します。ARM compileは一時objectのみを使い、外部build/checkoutを変更しません。
当時残っていた統合作業:

1. 確認済みEthernet descriptors/netifとCDC/Mouse/Keyboard/EEPROM/WS2812を単一targetへ統合。identity/Flash配置を保持し、暗黙のstack差替えをしない。
2. CMake helperと上記main loopでlink。
3. Windows/iPadで169.254.7.1へアクセスし、pickerなしのassets、GET同期、SET/RESET/SAVE/PING、LED、reboot保存を検証。
4. iPad Wi-Fi併存、再接続、HTTP停滞復帰、network/SAVE負荷中の移動・buttons・shortcutsでdrop/stutter/stuckを確認。
5. Pages/localhostにAPIがない場合のSerial fallbackを確認。

この初期作業ではpush/upload/統合実機試験は未実施でした。その後の完了状況は冒頭を参照してください。
2026-09-23の結果: Node 54/54、firmware host、fsdata byte/header/determinism、実lwIP ARM object compileがPASS。Arduino Serial sketchはインストール済みPhilhower 6.1.1でcompile成功、program 68,320 B/global RAM 10,604 B。validConfig/parseLine/createLineReader/createProtocol/createHeartbeatも前revisionと不変確認しました。

## EN

### Current target

The integrated Pico SDK target is now [firmware/redpoint_pico](../firmware/redpoint_pico/README.md).
USB Ethernet + HID + Flash + LED and the separate Pointer/Wheel settings were hardware-verified
on Windows/iPad through C.2. See [C.2](../firmware/redpoint_pico/MILESTONE_C2.md) and
[C.3 UI validation](../firmware/redpoint_pico/MILESTONE_C3.md) for milestone-specific results.
C.3 iPad display and touch operation have also been user-confirmed.
The API/lwIP integration details below remain the architecture reference.

### Original adapter-stage boundary (historical)

This section records the earlier compile-only integration stage, before the integrated Pico target.
It does not describe the current implementation status.


The frontend supports same-origin HTTP first, with the existing Web Serial path
as fallback. `firmware/http_lwip` supplies **standard lwIP httpd extensions**, not
a replacement TCP/HTTP server. `config_command.cpp` is the single command core
used by both adapters.

The existing Arduino sketch still builds CDC + Mouse + Keyboard. It does **not**
enable Ethernet just by compiling this commit. The validated Ethernet experiment
is a separate Pico SDK/TinyUSB project at
`E:/projects/tinyusb-master/examples/device/net_lwip_webserver`:

- `main.c` sets 169.254.7.1, mask 255.255.0.0, gateway 0.0.0.0.
- `lwipopts.h` uses `NO_SYS=1`, DHCP disabled, standard httpd/fs.
- TinyUSB network callbacks feed lwIP from the main task.
- Its USB descriptors and platform initialization are separate from the Arduino
  HID sketch. No changes were made to that external checkout.

The HTTP adapter/core and generated assets were compiled with that experiment's
actual ARM compiler/includes and actual `httpd.c`/`fs.c`. This verifies compilation,
**not a linked Ethernet + HID firmware or hardware operation**.

### Frontend and API

At startup the browser sends a GET **protocol command** via HTTP POST to the
relative URL `api/command`. A valid framed GET/config response selects HTTP and
becomes the initial synchronized session. It works without Web Serial or a secure
context. Failed probe closes/aborts the HTTP session and starts existing Serial
discovery/permission handling. GitHub Pages remains usable through this fallback.

```http
POST /api/command HTTP/1.1
Content-Type: text/plain
Cache-Control: no-store
Content-Length: 4

GET
```

Responses have `Content-Type: text/plain; charset=utf-8` and `Cache-Control: no-store`:

```text
@CONFIG {"ok":true,"command":"GET","config":{...}}
```

- GET, SET, RESET, SAVE and PING use the existing protocol parser/state machine.
- Protocol errors remain HTTP 200 with `@CONFIG {"ok":false,...}`.
- Maximum request body is **96 bytes including line endings**; one command only.
  Leading/trailing CR/LF is permitted for existing GET resync. Empty, embedded
  NUL/control/non-ASCII, multiple-command and overlong command bodies are rejected
  before execution. HTTP 400/413 indicate framing/size failures.
- HTTP GET `/api/command` returns 405. Unsupported POST URI/content type, duplicate
  content headers and Transfer-Encoding are rejected. Content-Length is required.
- No CORS is enabled. If Origin is supplied it must match `http://` plus Host;
  cross-origin browser writes are rejected. This is a local, unauthenticated API,
  not an Internet-facing service. It does not implement HTTPS.
- Up to four POST contexts have separate fixed buffers. No shared global response
  buffer is reused while lwIP is transmitting it. Interrupted bodies are discarded.
  Completed bodies execute once in `httpd_post_finished`; `fs_close_custom` releases
  response storage. Busy contexts/LED hardware latch return 503.
- HTTP Disconnect is logical: abort pending fetch, stop heartbeat, clear session.
  Connect starts a new HTTP GET sync; it does not close the Ethernet interface.
- Every fetch has its own abort controller, deadline and generation. A timed-out
  response cannot complete the newer GET resync even if abort is ignored.
- The existing 2-second protocol timeout and resync queue stay authoritative.
  Fetch/status failures follow the existing transport error/disconnect path.

The frontend contains no device IP address. The page's origin determines the API
host. CSP permits `connect-src 'self'`; no external network service is added.

### Attach to the network firmware target

From the CMake file defining the **combined RedPoint network target**:

```cmake
include("/path/to/red_dot_pinting/firmware/http_lwip/redpoint_http.cmake")
redpoint_attach_http(your_target)
```

The target must already compile standard lwIP `src/apps/http/httpd.c` and `fs.c`
(as the TinyUSB example does). The helper enables POST/custom files, includes
`config_command.cpp`, `config_http.cpp` and `redpoint_httpd.cpp`, and generates
fsdata with Python's standard library. Do **not** compile generated fsdata as a
separate source: `fs.c` includes it via `HTTPD_FSDATA_FILE`.

Do not add `config_command.cpp` twice if the target already builds all RedPoint
sources. Use the source list explicitly when composing the combined target.

The target also needs these **real platform implementations**, not test stubs:

- `loadDeviceConfig`, `saveDeviceConfig`, `takeConfigFlashWrite` with existing
  record v2 / v1 migration semantics; the Arduino implementation is in
  `config_storage.cpp` and uses EEPROM.
- Status LED API (`status_led.cpp` uses Arduino/Adafruit NeoPixel).
- Button action validation/formatting (`button_action.cpp`) with its HID backend.
- Serial adapter `config.cpp` / `config_serial.h` when CDC is retained.

These dependencies are why the standalone network-only example cannot simply
link the new command core without composing the existing RedPoint platform.
The helper intentionally supplies no fake SAVE success, dummy LED, or separate
RAM-only configuration backend.

At boot, initialize the real LED and storage and establish the baseline exactly
as the Arduino sketch does. After netif initialization call standard `httpd_init()`.
Keep the known working IP/mask, no gateway and no DHCP settings in the netif layer.

All config execution must happen on **one main-loop owner**, not an IRQ or a
concurrent TCP/IP thread. The reference NO_SYS network task satisfies this only
when serviced from that same loop. After Serial/network service:

1. Consume `takeConfigChange()` and clear pointer/scroll remainders for SET/RESET.
2. Consume `takeConfigFlashWrite()` and perform the existing PS/2 post-Flash
   FIFO/frame resynchronization (including failure paths).
3. Run `statusLedUpdate()` and continue normal HID/PS/2 service.

The Arduino loop was changed to consume the shared change flag, so future HTTP
mutations do not bypass the existing accumulator reset behavior.

### Static files

`tools/generate_http_fsdata.py` packs the current Configurator into normal
`struct fsdata_file` records, with MIME types, Content-Length and no-store headers.
No minification, CDN, server-side templating or separate frontend copy is used.

```powershell
python tools/generate_http_fsdata.py --output build/http/fsdata_redpoint.c
```

Generated paths: `/`, `/index.html`, `/app.js`, `/shortcuts.js`, `/style.css`.
`/api/command` is handled dynamically by POST callbacks. Files are embedded in
Flash; generation reruns when any input asset changes.

### Validation and hardware handoff

```powershell
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
python tests/http_fsdata_test.py
python tests/compile_http_lwip.py E:/projects/tinyusb-master/examples/device/net_lwip_webserver/build/compile_commands.json
arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint
```

Host tests cover Serial/HTTP response equality, mutations, malformed/batched
bodies, fragmented pbuf delivery, interrupted POST cleanup, pool exhaustion,
headers and static asset byte equality. ARM compilation uses temporary objects
only and leaves the external build/checkouts unchanged.

Remaining hardware integration:

1. Compose the working TinyUSB Ethernet descriptors/netif and the actual
   CDC + Mouse + Keyboard + EEPROM + WS2812 implementations into one build target.
   Preserve USB identities and Flash layout; no stack swap was made implicitly.
2. Link the target with the CMake helper and the main-loop integration above.
3. On Windows/iPad, open `http://169.254.7.1/` and verify assets, GET sync,
   SET/RESET/SAVE/PING, LED states and reboot persistence without a picker.
4. Verify iPad Wi-Fi Internet coexistence, disconnect/reconnect, and stalled HTTP
   response recovery. Test movement/buttons/shortcuts during network and SAVE
   traffic for packet drops, stutter or stuck inputs.
5. Verify GitHub Pages/localhost without the device HTTP API still use Serial.

No remote push, hardware upload, or combined-target hardware test was performed.

Validation results (2026-09-23): Node **54/54 passed**; firmware host tests passed;
fsdata byte/header/determinism test passed; actual lwIP ARM object compilation
passed. The default Arduino Serial sketch compiled with the currently installed
Philhower **6.1.1** core: **68,320 bytes program / 10,604 bytes global RAM**.
`validConfig`, `parseLine`, `createLineReader`, `createProtocol`, and `createHeartbeat`
were also compared against the prior revision and remain unchanged.
