# Milestone C.1 — Middle-held native HID scrolling

## Behavior and scope

A/B/C were confirmed on Windows/iPad by the user. C.1 retains the USB,
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

## Changed files (relative to firmware/redpoint_pico)

- src/input_runtime.cpp: logical scroll mode and fractional motion routing/reset.
- src/hid_state.cpp and src/hid_state.h: wheel/pan queue/API; suppress Middle bit.
- tests/native/platform.cpp and test_platform.h: capture all five mouse fields.
- tests/native/hardware_tests.cpp: update prior Middle expectations.
- tests/native/scroll_tests.cpp: new scroll behavior/recovery coverage.
- tests/native/integration.cpp and tests/run_integration.py: run scroll suite.
- README.md and MILESTONE_C1.md: current behavior and validation report.

## Build and validation

Build the existing configured target:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
```

Successful final ELF link and UF2 generation; no build warnings.
Flash: 137,020 B (0.82% of 16,380 KiB firmware region).
RAM: 41,824 B, plus 4,096 B main stack in SCRATCH_Y.
These are linker allocations, not runtime high-water measurements.
UF2: `E:/projects/red_dot_pinting/firmware/redpoint_pico/build/redpoint_reva.uf2`
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

## Hardware verification remaining for C.1

After the user uploads the UF2, verify on Windows and iPad:

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
USB timing under sustained traffic remain unverified. No upload or remote push
was performed. Prior A/B/C hardware confirmation does not establish C.1 success.
