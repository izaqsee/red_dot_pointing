# RedPoint RevA Pico SDK — Milestone C.2

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

独立したRP2040 targetです。current TinyUSBのCDC-NCM、CDC ACM Serial、2つのHID interfaceを構成します。A/B/Cは実機確認済みで、USB/network/HTTP/frontend architectureをfreezeしています。CではGPIO12/13のTrackPoint PS/2、GPIO3/2/4の物理ボタン、TinyUSB入力report、設定保存、GPIO23のWS2812を追加しました。
[C.2](MILESTONE_C2.md)にPointer/Wheel独立設定、v3移行、検証結果を記録しています。C.1のnative scrollingはWindows/iPadでユーザー確認済みです。[C](MILESTONE_C.md)は当時の配置・意図的差分・テスト・実機検証・変更一覧、[B](MILESTONE_B.md)はB時点の履歴です。

### 依存関係とarchitecture

実機確認済みの参照元は`E:/projects/tinyusb-master/examples/device/net_lwip_webserver`です。`REDPOINT_TINYUSB_PATH`は必須で、そのcheckoutのRP2040 family support、USB core、NCM driver、lwIPを直接使用します。`PICO_TINYUSB_PATH`も同じcheckoutに指定し、Arduino-Pico同梱driverは使いません。依存ツールは事前インストールが必要です。参照checkoutへは書き込まず、生成物はtargetのbuild directoryへ置きます。

参照TinyUSB HEADは`b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48`です。local変更も検出するため、`reference.json`は実際のNCM、USB core、参照exampleのSHA-256を記録します。`tests/check_build.py`はhashとcompilerが選択したNCM sourceを検証します。TinyUSBを意図的に変更する際は、新driverをreview・再検証してから記録を更新してください。

BSPは実機確認したUSB参照と同じ`raspberry_pi_pico` / Pico SDK `pico`です。RevAのpinはplatform_io.cppで明示初期化します。実Flash容量はpicotool確認済みの16 MiBで、末尾4 KiBをlinkerから除外して設定専用に予約します。SDK USB/UART stdioは無効、CDCはcommand adapterが所有します。参照BSPのGPIO25 LED初期化は維持し、実際のstatus表示はGPIO23のWS2812を使います。

core 0のmain loopがTinyUSB、lwIP timer、CDC command、物理入力、HIDを処理します。network backpressureはlwIP内で待ち続けず`ERR_MEM`を返します。lwIP allocation失敗時は受信frameを破棄しNCM受信を再開します。再送・復旧と継続負荷は実機検証が必要です。

IPv4は**169.254.7.1**、mask **255.255.0.0**、gateway **0.0.0.0**です。DHCP/AutoIP clientは無効、DHCP/DNS serverはlinkしません。hostが自身のlink-local addressを設定します。標準lwIP httpdは生成ROM fsdataから現行Configuratorを配信し、既存HTTP adapterと`config_command.cpp`で`POST /api/command`を処理します。この統合のためのfrontend変更はありません。

### USB descriptor配置

full-speed configurationは1つ、interfaceは6つ、総長**210 bytes**です。
`9 + 85 (NCM) + 66 (CDC ACM) + 25 (mouse) + 25 (keyboard)`。
bus power、申告最大100 mA、remote wakeupなし。方向はhostから見たものです。

| Interface | Alternate | 機能 | Endpoint address | Type / 方向 | 最大packet | 間隔 |
|---|---|---|---|---|---|---|
| — | — | EP0 | `0x00`, `0x80` | Control OUT / IN | 64 B | — |
| 0 | 0 | NCM control | `0x81` | Interrupt IN | 64 B | 50 ms |
| 1 | 0 | NCM data inactive | なし | — | — | — |
| 1 | 1 | NCM data | `0x02`, `0x82` | Bulk OUT / IN | 64 B 各 | — |
| 2 | 0 | CDC ACM control | `0x83` | Interrupt IN | 8 B | 1 ms |
| 3 | 0 | CDC ACM data | `0x04`, `0x84` | Bulk OUT / IN | 64 B 各 | — |
| 4 | 0 | Boot mouse | `0x85` | Interrupt IN | 8 B | 10 ms |
| 5 | 0 | Boot keyboard | `0x86` | Interrupt IN | 8 B | 10 ms |

non-control endpoint addressは8つ、番号1–6を使います。番号2と4のIN/OUT共有は意図的でRP2040が対応します。異なる機能間のaddress重複はありません。NCM IADはinterface 0–1、CDC IADは2–3。device classは`EF/02/01` (IAD composite)、USB versionは2.01です。参照BOS / Microsoft OS 2.0 `WINNCM` descriptorは**interface 0だけ**に適用しSerialを対象にしません。BOSは33 B、Microsoft OS 2.0 setは178 Bです。

開発用VID/PIDは**CAFE:4019**、productは`RedPoint RevA Milestone A`、serialはPico SDKのunique board ID由来です。実験用であり製品用USB ID割当ではありません。参照のnetwork専用PIDは流用しません。locally administeredな参照MACを維持します。複数台のMAC一意性は未対応なので、この段階ではhost linkにつき1台を使います。

| TinyUSB設定 | 値 |
|---|---|
| `CFG_TUSB_MCU` / OS | `OPT_MCU_RP2040` / `OPT_OS_PICO` (BSP) |
| `CFG_TUD_ENABLED` / `CFG_TUD_MAX_SPEED` | `1` / `OPT_MODE_FULL_SPEED` |
| `CFG_TUD_NCM` / `CFG_TUD_ECM_RNDIS` | `1` / `0` |
| `CFG_TUD_CDC` / `CFG_TUD_HID` | `1` / `2` |
| MSC / MIDI / vendor | 無効 |
| EP0 / CDC endpoint buffer / HID endpoint buffer | 64 / 64 / 8 B |
| CDC RX / TX FIFO | 256 / 256 B |
| NCM IN / OUT NTB maximum | 2048 / 4096 B |
| NCM IN / OUT NTB count | 1 / 1 |
| `CFG_TUD_NET_MTU` | 1514 B Ethernet frame; lwIP IP MTU 1500 B |

Mouse report protocolは5 bytes（buttons、x、y、wheel、pan）、boot protocolは3 bytesです。Keyboardは8 bytesを送り、control SET_REPORTで1-byte LED outputを受けます（OUT endpointなし）。`hid.h`は非blocking送信APIを公開します。C backendは物理移動・ボタン・shortcutを送り、endpoint busy中のpress/release順序を保持します。USB resume/再enumeration後はremote wakeupを使わずneutral→現在stateへ再同期します。CDCはterminalがDTRを立てたとき処理します。Bではecho-only taskを廃止し、HTTPと同じcoreへ改行区切り設定commandを接続しました。

### Buildと検証

RedPoint repo rootからPowerShellで実行します。以下はインストール済みツールのpathです。

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' `
  -S firmware/redpoint_pico -B firmware/redpoint_pico/build -G Ninja `
  -DREDPOINT_TINYUSB_PATH=E:/projects/tinyusb-master `
  -DPICO_SDK_PATH=E:/projects/pico-sdk `
  '-DPICO_TOOLCHAIN_PATH=C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin' `
  -Dpicotool_DIR=E:/projects/picotool-2.3.1-x64-win/picotool `
  -Dpioasm_DIR=E:/projects/pico-sdk-tools-2.3.1-x64-win/pioasm `
  -DCMAKE_MAKE_PROGRAM=C:/Users/intel/AppData/Local/Microsoft/WinGet/Links/ninja.exe `
  -DCMAKE_BUILD_TYPE=MinSizeRel
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/check_milestone_c2.py
```

検査はPython標準libraryだけを使うため、別のPython 3でも実行できます。C macroだけでなく**link済みELF**のconfiguration/interface数、endpoint address/type/size/衝突、IAD範囲、HID report size/boot class、BOS/MS OS descriptorを検査します。source由来と、UF2がRP2040形式で生成binaryと同じpayloadであることも確認します。

出力は`build/redpoint_reva.{elf,elf.map,bin,hex,uf2}`です。このcommandはUSB deviceへアクセス・uploadしません。

C.2時点のbuildはFlash **139,732 B / 使用可能16,380 KiB (0.83%)**、RAM **41,840 B / 256 KiB (15.96%)**、別途Scratch Y stack **4,096 B**、UF2 **279,552 B**です。runtime high-waterではなくlinker allocationです。XIP **0x10FFF000–0x10FFFFFF**のconfig sectorはELF/UF2に含めません。

### 実機受入確認とA/B freeze baseline

Windows/iPadでユーザー確認済み：composite enumeration、NCM Code 10なし、Windows HTTP 200/Chrome Configurator、iPad Safari/Chrome Configuratorの自動GET同期/Connected、iPad Wi-Fi Internet併存。USB descriptor、endpoint、NCM source、netif設定はfreezeしています。次の手順はCのregression検証用として保持します。

Windows Device Managerの**表示 → 接続別**で、同じ`VID_CAFE&PID_4019`のcomposite parentと全機能を確認します。

- USB Composite Device：警告なし。
- Network adapters：USB NCM / USB NCM Host Device、Windows NCM driver、Code 10なし、interface associationは`MI_00`から。
- Ports (COM & LPT)：USB Serial Device (COMx)、`MI_02`、DTRを立て改行区切りGET/PING。
- Mice and other pointing devices：HID-compliant mouse、`MI_04`。
- Keyboards：HID Keyboard Device、`MI_05`（HID devicesにも表示）。

表示名はWindowsの言語/versionにより変わるのでparent/MIの関連も確認します。cold attach、抜差し、再起動、suspend/resume後も4機能が同時に存在することを確認します。`ipconfig`でhostが169.254/16、このdevice由来gatewayなしであることを確認し、`http://169.254.7.1/`を開きます。CDC commandとnetworkの同時処理、Windows NCM安定性、HID boot/report protocol、iPad Wi-Fiを維持したlink-localアクセスも確認します。descriptor解析とbuild成功だけでは実機成功を保証しません。

Cの物理入力・Flash保存・WS2812、C.1のnative scrollingはユーザー確認済みです。C.2設定/default vertical方向とC.3のiPad表示・touch操作もユーザー確認済みです。消費電力/suspend適合性、runtime stack/heap high-water、長時間network throughput、MAC一意性、Windows各version互換性は未検証です。

### ファイル

[C](MILESTONE_C.md)は当時の変更一覧、[C.1](MILESTONE_C1.md)はnative scrollingの変更一覧です。参照元licenseはLICENSE.referenceと派生source内に保持しています。

## EN

Independent RP2040 target: current TinyUSB CDC-NCM + CDC ACM Serial + two HID
interfaces. Milestones A/B/C are hardware-verified and their USB/network/HTTP/frontend
architecture is frozen. Milestone C adds GPIO12/13 TrackPoint PS/2, GPIO3/2/4
physical buttons, TinyUSB input reports, persistent config and GPIO23 WS2812.
See [MILESTONE_C2.md](MILESTONE_C2.md) for separate Pointer/Wheel settings, v3 migration and current validation. C.1 native scrolling is user-confirmed on Windows/iPad. See [MILESTONE_C.md](MILESTONE_C.md) for the historical C layout, intentional differences, tests,
hardware validation and the complete change list. [MILESTONE_B.md](MILESTONE_B.md)
is the historical B report.

### Dependencies and architecture

The verified reference is
`E:/projects/tinyusb-master/examples/device/net_lwip_webserver`.
`REDPOINT_TINYUSB_PATH` is mandatory: the build loads that checkout's RP2040
family support, USB core, NCM driver and lwIP directly. It explicitly sets
`PICO_TINYUSB_PATH` to the same checkout, never the Arduino-Pico bundled driver.
Dependencies must already be installed. No source is written to the reference
checkout; all generated files are in the target's build directory.

Reference TinyUSB HEAD: `b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48`. The checkout can contain
local changes, so `reference.json` also records SHA-256 hashes of the actual NCM,
USB core and reference example files. `tests/check_build.py` verifies these hashes
and the NCM source selected by the compiler. If intentionally changing TinyUSB,
review and revalidate the new driver before updating that record.

The baseline BSP remains `raspberry_pi_pico` / Pico SDK `pico`, matching the tested
USB reference. RevA pins are explicitly initialized in platform_io.cpp. Physical
Flash is now correctly declared as 16 MiB (picotool-confirmed); its last 4 KiB is
excluded from the linker and reserved for config. SDK USB/UART stdio are disabled;
CDC belongs to the command adapter. The reference BSP's GPIO25 LED initialization
is retained; the status renderer uses the actual onboard WS2812 on GPIO23.

The main loop services TinyUSB, lwIP timers, CDC commands, physical input and HID on
core 0. Network backpressure returns `ERR_MEM` instead of spinning inside lwIP and
starving the other functions. lwIP allocation failure drops the received frame
and renews NCM reception. Protocol retransmission/recovery and sustained load
need hardware testing.

IPv4 is **169.254.7.1**, mask **255.255.0.0**, gateway **0.0.0.0**.
DHCP and AutoIP clients are disabled; no DHCP server or DNS server is linked.
The host assigns its own link-local address. The standard lwIP httpd serves the
current Configurator from generated ROM fsdata and handles `POST /api/command`
through the existing HTTP adapter and `config_command.cpp`. No frontend code changes.

### USB descriptor allocation

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
The C backend sends physical motion/buttons/shortcuts. It retains press/release
ordering while endpoints are busy and resynchronizes neutral/current state after
USB resume/re-enumeration without remote wakeup. CDC is serviced when
the terminal opens with DTR asserted. In Milestone B it accepts newline-delimited
configuration commands through the same core as HTTP; the echo-only task is removed.

### Build and validation

PowerShell, from the RedPoint repo root, using the installed tool paths:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' `
  -S firmware/redpoint_pico -B firmware/redpoint_pico/build -G Ninja `
  -DREDPOINT_TINYUSB_PATH=E:/projects/tinyusb-master `
  -DPICO_SDK_PATH=E:/projects/pico-sdk `
  '-DPICO_TOOLCHAIN_PATH=C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin' `
  -Dpicotool_DIR=E:/projects/picotool-2.3.1-x64-win/picotool `
  -Dpioasm_DIR=E:/projects/pico-sdk-tools-2.3.1-x64-win/pioasm `
  -DCMAKE_MAKE_PROGRAM=C:/Users/intel/AppData/Local/Microsoft/WinGet/Links/ninja.exe `
  -DCMAKE_BUILD_TYPE=MinSizeRel
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/check_milestone_c2.py
```

The test uses only Python's standard library; another Python 3 interpreter is fine.
It inspects the **linked ELF**, not just C macros: configuration/interface counts,
endpoint addresses/types/sizes and collisions, IAD scope, HID report sizes and
boot classes, BOS/MS OS descriptors. It also verifies source provenance and checks
that the UF2 is RP2040 format with a payload matching the generated binary.

Build outputs: `build/redpoint_reva.{elf,elf.map,bin,hex,uf2}`.
These commands do not upload or access a USB device.

Milestone C.2 build: Flash **139,732 B / 16,380 KiB available (0.83%)**,
RAM **41,840 B / 256 KiB (15.96%)**, plus **4,096 B** Scratch Y stack reservation.
UF2 **279,552 B**. Allocation is a linker measurement, not runtime high-water use.
The config sector at XIP **0x10FFF000–0x10FFFFFF** is not included in ELF/UF2.

### Hardware acceptance and frozen A/B baseline

User-confirmed on Windows/iPad: composite enumeration, NCM without Code 10,
Windows HTTP 200/Chrome Configurator, iPad Safari/Chrome Configurator with
automatic GET sync/Connected, and simultaneous iPad Wi-Fi Internet.
The USB descriptors, endpoints, NCM source and netif configuration are frozen.
The following checks are retained for Milestone C regression validation.

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

Milestone C physical input, Flash persistence and WS2812 are user-confirmed. C.1 native scrolling is user-confirmed; C.2 settings/default vertical direction and C.3 iPad display/touch operation are also user-confirmed.
Power draw/suspend compliance,
runtime stack/heap high-water marks, long-running network throughput, unique MACs
and Windows compatibility across OS versions remain unverified.

### Files

See [MILESTONE_C.md](MILESTONE_C.md) for the historical C change list; [MILESTONE_C1.md](MILESTONE_C1.md) lists the native-scrolling changes. Original reference
licenses are retained in LICENSE.reference and the adapted source files.
