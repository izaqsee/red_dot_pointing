# Milestone B — Configurator over the frozen USB Ethernet architecture

Milestone A was confirmed on hardware by the user: CDC-NCM + CDC Serial + HID
Mouse + HID Keyboard, 169.254.7.1/16, no gateway/DHCP, Windows without NCM Code 10,
Windows HTTP 200, iPad Safari HTTP and simultaneous Wi-Fi Internet. None of those
descriptors, endpoints, NCM sources or netif settings were changed for B.
`milestone_a_freeze.json` records the descriptor/HID/USB configuration and frontend
source hashes; `reference.json` retains the external TinyUSB provenance checks.
The USB product string remains the A string deliberately, as part of the freeze.

## Static assets and command path

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

## Pico backend and supported commands

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

## Build and tests (2026-09-23)

Use the configure command in README.md, then from the repo root:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/check_milestone_b.py
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
& 'C:/nvm4w/nodejs/node.exe' --test tests/configurator.test.cjs
& 'C:/Program Files/Inkscape/bin/python.exe' tests/run_firmware_tests.py
& 'C:/Program Files/Inkscape/bin/python.exe' tests/http_fsdata_test.py
& 'C:/Program Files/Inkscape/bin/python.exe' tests/compile_http_lwip.py firmware/redpoint_pico/build/compile_commands.json
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
Milestone B has not been uploaded or tested on hardware by the agent.

Output: `E:/projects/red_dot_pinting/firmware/redpoint_pico/build/redpoint_reva.uf2`.
Flash/BIN: **130,308 B (6.21% of 2 MiB)**. RAM region: **39,156 B (14.94% of
256 KiB)** plus **2,048 B** Scratch Y stack reservation. UF2 container: **261,120 B**.
These are linker allocations, not runtime heap/stack measurements.

## Hardware confirmation by the user

1. When ready, install this UF2 yourself; no upload has been performed here.
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

## Milestone C

Real EEPROM/Flash persistence with validation/readback, flash-safe USB/interrupt
coordination and actual post-flash TrackPoint resync; PS/2 TrackPoint and three
physical buttons; RevA peripheral pins and physical WS2812 rendering; physical HID
action application; runtime heap/stack/load measurements. No dummy persistence or
resync implementation is hidden behind a success response.

## Changed files

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
