# Firmware build / upload

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

USB Ethernet版は[`firmware/redpoint_pico`](../../firmware/redpoint_pico/README.md)が対象です。
Pico SDK版の依存関係・configure/build・UF2出力はそちらを参照してください。生成UF2を対応ボードのBOOTSELモードで書き込みます。
設定sectorやboard/Flash条件は[Pico C報告](../../firmware/redpoint_pico/MILESTONE_C.md)を確認してください。

### Pico SDK版をbuildだけする

初回のconfigureが完了している環境で、repository rootから実行します。`cmake`とbuild tool（Ninja）をPATHから利用できるようにしてください。

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

生成UF2: `firmware/redpoint_pico/build/redpoint_reva.uf2`。このコマンドはbuildのみで、実機へのuploadは行いません。firmware sourceやConfigurator assetsの変更後も同じコマンドで再buildできます。
初回configureの手順は[Pico SDK target](../../firmware/redpoint_pico/README.md#build-and-validation)を参照してください。

以下はREADMEから移動した**Arduino版 `firmware/redpoint` 専用**の手順です。
VS Code taskとCOM helperはArduino CLIを呼び、USB Ethernet版のbuild/uploadには使いません。
helperのVID_2E8A検出条件をPico SDK compositeへそのまま適用しないでください。

すべてのコマンドは、特記がなければrepository rootで実行します。
[開発資料一覧](README.md) · [プロジェクト概要](../../README.md)

### Firmwareのbuild / upload

実機確認済みの環境:

- Arduino RP2040 (Philhower) core **6.1.0**
- FQBN: `rp2040:rp2040:vccgnd_yd_rp2040`
- Windows / Arduino CLI **1.5.2-rc.1**

Arduino CLIをPATHに追加し、上記coreをインストール済みの環境で、repo直下から実行します。
Arduino IDE同梱のCLIを使う場合は、`arduino-cli`をその実行ファイルのパスに置き換えます。

```powershell
arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\upload-redpoint.ps1
```

VS Codeでは既存の`.vscode/tasks.json`から次のタスクを実行できます。

- **RedPoint: Build**（既定のbuild task）
- **RedPoint: Build & Upload**（RedPoint自動検出 → build → upload）

既存の**Ctrl+Alt+U**は同じ`RedPoint: Build & Upload`を呼び出します。
COM番号が変更されても、tasks.jsonの手動編集は不要です。Buildは従来どおりcompileのみです。
helperはWindows標準のCIMでUSB serial portを列挙し、`PNPDeviceID`の`VID_2E8A`で候補を絞ります。
PIDを固定せず、候補が1台でも全候補へ115200 baudでGETを送り、
`@CONFIG` JSONの`ok: true`かつ`command: "GET"`を確認します。
応答待ちは各候補1,200 ms。debug行／不正JSONを無視し、分割受信・CR／LFに対応します。
probe後はportをclose／disposeし、認定が1台だけならそのCOMを既存の
`arduino-cli compile --upload --port ...`へ渡します。bootloaderへの遷移はArduino CLIに任せます。
認定0台・複数台では候補と結果を表示して中止し、推測でportを選びません。

uploadせず検出だけを確認する場合（GETのみ送信、設定変更なし）:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\upload-redpoint.ps1 -DetectOnly
# 別の作業ディレクトリから。空白を含むパスも指定可能:
# REDPOINT_WORKSPACEにrepositoryのpathを設定（空白を含むpathも可）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:REDPOINT_WORKSPACE\tools\upload-redpoint.ps1" -WorkspacePath "$env:REDPOINT_WORKSPACE" -DetectOnly
```

`RedPoint detected on COMxx`が出れば検出成功です。実際にbuild／uploadするときは`-DetectOnly`を外します。
CLIの終了コードはそのままtaskへ伝わり、検出失敗は終了コード1になります。
追加のPowerShell moduleは不要です。Windows PowerShell 5.1／PowerShell 7に対応します。

**実機確認:** ConfiguratorをDisconnectしSerial Monitorを閉じ、`-DetectOnly`で検出を確認します。
USBの挿し直しでCOM番号が変わっても再検出されること、未接続時とRedPoint複数接続時は中止することを確認してください。
uploadが必要なときにCtrl+Alt+Uを実行し、検出したCOMがCLIに渡ることを確認します。

**制限:** 正常起動してGETに応答するfirmwareが必要です。bootloader状態、他アプリによるport占有、
VIDが異なるfirmwareは検出できません。待ち時間は候補数に比例し、OSのport open／close時間は別途かかります。
GETと同じ応答を実装した別firmwareは区別できません。probeが失敗した候補のidentityも確定できません。
検出後からbuild／upload完了までUSBを抜き差ししないでください（COM再割当の競合は防げません）。
GET probeはLEDのConfigurator active判定を一時的に更新しますが、RAM設定やFlashは変更しません。

実機を使わないhelperテスト:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\upload-redpoint.test.ps1
```

Serial／CIMはmock、CLIは一時的なfixtureに置き換えます。実機probe・実機uploadは実行しません。

Arduino IDEでは`firmware/redpoint/redpoint.ino`を開きます。
同じフォルダの`config*`（設定・レコード・保存）、`button_action.*`（Actionと所有数）、
`keyboard_mapping.*`（HID usageからcore APIへの変換）も必要です。
upload前にConfiguratorのDisconnectを押し、Serial Monitorなども閉じてください。
ファームウェアはcore標準のMouse / Keyboardライブラリを使用します。USB Stackは既存のPico SDKを維持してください。
Keyboard追加後はUSBが再列挙され、COM番号が変わる場合があります。

## EN

The USB Ethernet target is [`firmware/redpoint_pico`](../../firmware/redpoint_pico/README.md). Its README covers dependencies, configure/build commands, and UF2 output. Install the generated UF2 on the supported board in BOOTSEL mode. See the [Milestone C report](../../firmware/redpoint_pico/MILESTONE_C.md) for the config sector and board/Flash requirements.

### Build the Pico SDK target without uploading

After the initial configure step, run this from the repository root with `cmake` and the build tool (Ninja) available on PATH:

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
```

Output: `firmware/redpoint_pico/build/redpoint_reva.uf2`. This command builds only; it does not upload to hardware. Use the same command after changing firmware sources or Configurator assets.
For initial configuration, see the [Pico SDK target](../../firmware/redpoint_pico/README.md#build-and-validation).

The following instructions, moved from the README, apply **only to the Arduino target `firmware/redpoint`**. The VS Code task and COM helper invoke Arduino CLI; they do not build/upload the USB Ethernet target. Do not apply the helper's VID_2E8A detection assumptions to the Pico SDK composite device.

Run commands from the repository root unless stated otherwise.
[Development index](README.md) · [Project overview](../../README.md)

### Firmware build / upload

Hardware-verified environment:

- Arduino RP2040 (Philhower) core **6.1.0**
- FQBN: `rp2040:rp2040:vccgnd_yd_rp2040`
- Windows / Arduino CLI **1.5.2-rc.1**

Install that core and put Arduino CLI on PATH. If using the CLI bundled with Arduino IDE, replace `arduino-cli` with its executable path.

```powershell
arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\upload-redpoint.ps1
```

VS Code tasks in `.vscode/tasks.json`:

- **RedPoint: Build**: default build task; compile only
- **RedPoint: Build & Upload**: auto-detect RedPoint → build → upload

The existing **Ctrl+Alt+U** binding invokes Build & Upload. No tasks.json edit is needed when the COM number changes.
The helper enumerates USB serial ports through standard Windows CIM and filters `PNPDeviceID` for `VID_2E8A`, without fixing a PID. Even with one candidate, it sends GET to every candidate at 115200 baud and checks for `@CONFIG` JSON with `ok: true` and `command: "GET"`.
Each response deadline is 1,200 ms. Debug lines/invalid JSON are ignored; fragmented input and CR/LF are supported.
After probing, the port is closed/disposed. Only one positively identified device is accepted; its COM port is passed to the existing `arduino-cli compile --upload --port ...`. Arduino CLI handles bootloader transition. Zero or multiple matches print results and stop rather than guessing.

Detection without upload (GET only; no setting changes):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\upload-redpoint.ps1 -DetectOnly
# From another working directory; paths containing spaces are supported:
# REDPOINT_WORKSPACE: repository path (including spaces, if applicable)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:REDPOINT_WORKSPACE\tools\upload-redpoint.ps1" -WorkspacePath "$env:REDPOINT_WORKSPACE" -DetectOnly
```

`RedPoint detected on COMxx` indicates success. Remove `-DetectOnly` to build/upload.
The CLI exit code propagates to the task; detection failure returns 1. No extra PowerShell module is required. Windows PowerShell 5.1 and PowerShell 7 are supported.

**Hardware checks:** Disconnect Configurator and close Serial Monitor, then run `-DetectOnly`. Verify re-detection after a USB replug changes the COM number, and rejection when no device or multiple RedPoints are connected. When an upload is intended, use Ctrl+Alt+U and confirm that the detected port reaches the CLI.

**Limitations:** A running firmware that answers GET is required. Bootloader mode, a port held by another application, or a different VID cannot be detected. Total waiting scales with candidate count; OS open/close delays are additional. Another firmware implementing the same GET response cannot be distinguished. Failed candidates have unknown identity. Do not replug during detection/build/upload; COM reassignment races cannot be prevented.
The GET probe temporarily updates Configurator-active LED state but changes neither RAM settings nor Flash.

Hardware-free helper test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\upload-redpoint.test.ps1
```

Serial/CIM are mocked and the CLI is a temporary fixture; no physical probe/upload occurs.

For Arduino IDE, open `firmware/redpoint/redpoint.ino`. The same folder's `config*` (settings/records/storage), `button_action.*` (actions/owners), and `keyboard_mapping.*` (HID usage to core APIs) are also required.
Disconnect Configurator and close other port users before uploading. Firmware uses the core Mouse / Keyboard libraries; retain the existing Pico SDK USB stack. Adding Keyboard can cause re-enumeration and a new COM number.
