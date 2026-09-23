# RedPoint RevA Pico SDK — Milestone B

Independent RP2040 target: current TinyUSB CDC-NCM + CDC ACM Serial + two HID
interfaces. Milestone A USB/network architecture is hardware-verified and frozen.
Milestone B adds the existing Configurator and shared command core over HTTP/CDC.
PS/2, EEPROM/Flash persistence, WS2812 and physical input handling are deferred.
See [MILESTONE_B.md](MILESTONE_B.md) for implementation, tests and hardware steps.

## Dependencies and architecture

The verified reference is
`E:/projects/tinyusb-master/examples/device/net_lwip_webserver`.
`REDPOINT_TINYUSB_PATH` is mandatory: the build loads that checkout's RP2040
family support, USB core, NCM driver and lwIP directly. It explicitly sets
`PICO_TINYUSB_PATH` to the same checkout, never the Arduino-Pico bundled driver.
Dependencies must already be installed. No source is written to the reference
checkout; all generated files are in the target's build directory.

Reference HEAD: `b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48`. The checkout can contain
local changes, so `reference.json` also records SHA-256 hashes of the actual NCM,
USB core and reference example files. `tests/check_build.py` verifies these hashes
and the NCM source selected by the compiler. If intentionally changing TinyUSB,
review and revalidate the new driver before updating that record.

The baseline BSP is `raspberry_pi_pico` / Pico SDK `pico`, matching the tested
reference. This uses a conservative 2 MiB flash region. It is not yet a full RevA
board definition; external peripheral pin assignments are not ported. SDK USB and
UART stdio are disabled; CDC belongs exclusively to the application. The reference
BSP still initializes its normal board LED GPIO. No WS2812 driver is included.

The main loop services TinyUSB, lwIP timers, CDC commands and neutral HID reports on
core 0. Network backpressure returns `ERR_MEM` instead of spinning inside lwIP and
starving the other functions. lwIP allocation failure drops the received frame
and renews NCM reception. Protocol retransmission/recovery and sustained load
need hardware testing.

IPv4 is **169.254.7.1**, mask **255.255.0.0**, gateway **0.0.0.0**.
DHCP and AutoIP clients are disabled; no DHCP server or DNS server is linked.
The host assigns its own link-local address. The standard lwIP httpd serves the
current Configurator from generated ROM fsdata and handles `POST /api/command`
through the existing HTTP adapter and `config_command.cpp`. No frontend code changes.

## USB descriptor allocation

One full-speed configuration, six interfaces, **210 bytes** total:
`9 + 85 (NCM) + 66 (CDC ACM) + 25 (mouse) + 25 (keyboard)`.
Bus powered, advertised maximum 100 mA, remote wakeup not advertised.
Directions below are from the host's perspective.

| Interface | Alternate | Function | Endpoint address | Type / direction | Max packet | Interval |
|---|---|---|---|---|---|---|
| — | — | EP0 | `0x00`, `0x80` | Control OUT / IN | 64 B | — |
| 0 | 0 | NCM control | `0x81` | Interrupt IN | 64 B | 50 ms |
| 1 | 0 | NCM data inactive | none | — | — | — |
| 1 | 1 | NCM data | `0x02`, `0x82` | Bulk OUT / IN | 64 B each | — |
| 2 | 0 | CDC ACM control | `0x83` | Interrupt IN | 8 B | 1 ms |
| 3 | 0 | CDC ACM data | `0x04`, `0x84` | Bulk OUT / IN | 64 B each | — |
| 4 | 0 | Boot mouse | `0x85` | Interrupt IN | 8 B | 10 ms |
| 5 | 0 | Boot keyboard | `0x86` | Interrupt IN | 8 B | 10 ms |

Eight unique non-control endpoint addresses, using endpoint numbers 1–6. IN/OUT
sharing numbers 2 and 4 is intentional and supported by RP2040; no address is
shared between functions. NCM IAD covers interfaces 0–1, CDC IAD covers 2–3.
The device class is `EF/02/01` (IAD composite), USB version 2.01.
The reference BOS and Microsoft OS 2.0 `WINNCM` descriptor apply **only to
interface 0**, so they do not claim the Serial interface. BOS length is 33 B;
Microsoft OS 2.0 set length is 178 B.

Development VID/PID: **CAFE:4019**; product `RedPoint RevA Milestone A`;
serial derives from Pico SDK's unique board ID. This experimental VID/PID is not
a production USB ID allocation. The reference network-only PID is not reused.
The locally administered reference MAC is retained; multi-device MAC uniqueness
is deferred (use one device per host link during this milestone).

| TinyUSB setting | Value |
|---|---|
| `CFG_TUSB_MCU` / OS | `OPT_MCU_RP2040` / `OPT_OS_PICO` (BSP) |
| `CFG_TUD_ENABLED` / `CFG_TUD_MAX_SPEED` | `1` / `OPT_MODE_FULL_SPEED` |
| `CFG_TUD_NCM` / `CFG_TUD_ECM_RNDIS` | `1` / `0` |
| `CFG_TUD_CDC` / `CFG_TUD_HID` | `1` / `2` |
| MSC / MIDI / vendor | disabled |
| EP0 / CDC endpoint buffer / HID endpoint buffer | 64 / 64 / 8 B |
| CDC RX / TX FIFO | 256 / 256 B |
| NCM IN / OUT NTB maximum | 2048 / 4096 B |
| NCM IN / OUT NTB count | 1 / 1 |
| `CFG_TUD_NET_MTU` | 1514 B Ethernet frame; lwIP IP MTU 1500 B |

Mouse report protocol sends five bytes (buttons, x, y, wheel, pan); boot protocol
sends three. Keyboard sends eight bytes and accepts the one-byte LED output via
control SET_REPORT (no OUT endpoint). `hid.h` exposes nonblocking send functions.
The diagnostic task sends only zero/neutral reports every 100 ms, without typing,
moving the pointer, or waking a suspended host. CDC is serviced when
the terminal opens with DTR asserted. In Milestone B it accepts newline-delimited
configuration commands through the same core as HTTP; the echo-only task is removed.

## Build and validation

PowerShell, from the RedPoint repo root, using the installed tool paths:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' `
  -S firmware/redpoint_pico -B firmware/redpoint_pico/build -G Ninja `
  -DREDPOINT_TINYUSB_PATH=E:/projects/tinyusb-master `
  -DPICO_SDK_PATH=E:/projects/pico-sdk `
  '-DPICO_TOOLCHAIN_PATH=C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin' `
  -Dpicotool_DIR=E:/projects/picotool-2.3.1-x64-win/picotool `
  -DCMAKE_MAKE_PROGRAM=C:/Users/intel/AppData/Local/Microsoft/WinGet/Links/ninja.exe `
  -DCMAKE_BUILD_TYPE=MinSizeRel
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/check_build.py
```

The test uses only Python's standard library; another Python 3 interpreter is fine.
It inspects the **linked ELF**, not just C macros: configuration/interface counts,
endpoint addresses/types/sizes and collisions, IAD scope, HID report sizes and
boot classes, BOS/MS OS descriptors. It also verifies source provenance and checks
that the UF2 is RP2040 format with a payload matching the generated binary.

Build outputs: `build/redpoint_reva.{elf,elf.map,bin,hex,uf2}`.
These commands do not upload or access a USB device.

Milestone B validation: final MinSizeRel build passed without warnings; Flash
**130,308 B / 2 MiB (6.21%)**, RAM **39,156 B / 256 KiB (14.94%)**, plus
**2,048 B** stack reservation in Scratch Y. UF2 size **261,120 B**; BIN size
**130,308 B**. RAM is linker allocation (including minimum heap reservation),
not measured runtime usage. Detailed test results are in MILESTONE_B.md.

## Hardware acceptance and frozen Milestone A baseline

User-confirmed on Windows/iPad: composite enumeration, NCM without Code 10,
Windows HTTP 200, iPad Safari HTTP, and simultaneous iPad Wi-Fi Internet.
The USB descriptors, endpoints, NCM source and netif configuration are frozen.
The following checks are retained for Milestone B regression validation.

In Windows Device Manager, use **View → Devices by connection** to identify the
same `VID_CAFE&PID_4019` composite parent and all four functions:

- USB Composite Device: no warning icon.
- Network adapters: USB NCM / USB NCM Host Device, bound to Windows NCM driver;
  no Code 10. Interface association starts at `MI_00`.
- Ports (COM & LPT): USB Serial Device (COMx), `MI_02`; newline-delimited GET/PING commands with DTR.
- Mice and other pointing devices: HID-compliant mouse, `MI_04`.
- Keyboards: HID Keyboard Device, `MI_05` (also represented under HID devices).

Friendly names vary by Windows language/version. Confirm parent/MI association
rather than relying only on displayed names. Verify enumeration after cold attach,
replug, reboot and suspend/resume; all four functions must remain present together.
Check `ipconfig` for host 169.254/16 and no gateway provided by this device; open
`http://169.254.7.1/`. Check concurrent CDC commands and network traffic, Windows NCM
stability, HID boot/report protocol, and iPad link-local access with Wi-Fi retained.
USB descriptor analysis and a successful build do not establish hardware success.

Before later milestones, port RevA-specific pins/peripherals, real HID input
and actual persistence with post-flash PS/2 recovery. Power draw/suspend compliance,
runtime stack/heap high-water marks, long-running network throughput, unique MACs
and Windows compatibility across OS versions remain unverified.

## Files

See [MILESTONE_B.md](MILESTONE_B.md) for the current change list. Original reference
licenses are retained in LICENSE.reference and the adapted source files.
