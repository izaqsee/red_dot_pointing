# USB Ethernet Configurator / lwIP integration

## Implemented boundary

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

## Frontend and API

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

## Attach to the network firmware target

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

## Static files

`tools/generate_http_fsdata.py` packs the current Configurator into normal
`struct fsdata_file` records, with MIME types, Content-Length and no-store headers.
No minification, CDN, server-side templating or separate frontend copy is used.

```powershell
python tools/generate_http_fsdata.py --output build/http/fsdata_redpoint.c
```

Generated paths: `/`, `/index.html`, `/app.js`, `/shortcuts.js`, `/style.css`.
`/api/command` is handled dynamically by POST callbacks. Files are embedded in
Flash; generation reruns when any input asset changes.

## Validation and hardware handoff

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
