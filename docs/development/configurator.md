# Configuratorの開発・運用

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

現行のPico SDK版はデバイス自身が`http://169.254.7.1/`でConfiguratorを配信します。
HTTP-firstとSerial fallbackの仕様は[USB Ethernet](../usb-ethernet.md)、設定schemaは[protocol](../protocol.md)を正とします。
C.3のUIはPointer / Wheel / Buttonsで、Keyboard Shortcutを選んだ行のRecordから記録します。
詳細は[C.2](../../firmware/redpoint_pico/MILESTONE_C2.md)・[C.3](../../firmware/redpoint_pico/MILESTONE_C3.md)を参照してください。

以下はREADMEにあった起動・自動接続・Recorder詳細を保存したものです。
「Middle sensitivity」「旧4項目firmwareでも使用可能」、
「デバイス確認値の別表示」は**移行前の状態**を記録した記述です。
現行Picoでは統合済み、Pointer/Wheel設定は独立、Middleはnative Wheel/Panのmodifierです。
現行UIはC.2 schemaを要求し、確認値の一部はscreen reader向けに保持しています。
旧Pages→新firmwareの互換aliasと、新UI→旧firmwareの制限は[protocol](../protocol.md)を参照してください。

すべてのコマンドは、特記がなければrepository rootで実行します。
[開発資料一覧](README.md) · [プロジェクト概要](../../README.md)

### Wheel感度カーブ

WheelのHorizontal / Verticalスライダーは低感度域を調整しやすい対数カーブです。左端はOFF (0)、次の位置は0.001×、以降100段階ごとに0.01× → 0.1× → 1× → 10×となります。表示・送信する値は実際の倍率で、小数6桁まで保持します。Pointerは従来の0〜10、0.01刻みです。

既存の設定値・初期値0.40×・保存形式は変わりません。読み込んだ値は操作するまで丸めて書き換えません。Wheel出力は引き続き整数tickで、低倍率はtickの発生頻度を下げます。1 tick自体のスクロール量はOS/application側で決まります。

### Configuratorを起動する

`configurator/`のVanilla HTML / CSS / JavaScriptだけで動作します。
framework、npm install、build、backend処理、外部API、CDNは不要です。
起動時にsame-originの`api/command`へGET commandをPOSTし、有効なRedPoint応答があれば
**USB Ethernet HTTP mode**へ自動接続します。APIがなければ既存の**Web Serial API**へfallbackします。
HTTP modeはpermission dialogやsecure contextを必要とせず、iPadのようにWeb Serialがない環境でも使用できます。
HTTP modeのDisconnectはlogical session切断で、ConnectでGET同期から再接続します。
FrontendにデバイスIPは固定していません。

現行Pico SDK targetでは、lwIP HTTP adapter、static asset生成、Ethernet、HID、Flash、LEDを統合済みです（C.2）。
Arduino sketch単独ではEthernetを有効化しません。
構成・CMake組込・実機確認手順は[USB Ethernet integration](../../docs/usb-ethernet.md)を参照してください。

repo直下で、Pythonによるローカルの静的ファイル配信を起動します。

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory configurator
```

Web Serial対応のChromium系ブラウザ（デスクトップ版ChromeまたはEdge）で[http://localhost:8000](http://localhost:8000)を開きます。
Web Serialはsecure context（localhostまたはHTTPS）で使用します。
ブラウザの対応状況とsecure contextを画面で検出し、利用できない場合は理由を表示します。
このHTTPサーバは静的ファイルの配信だけを行い、デバイスの設定値やSerial dataは受け取りません。
終了はターミナルでCtrl+Cです。

1. USBでRedPointを接続し、Serial Monitorなど他のポート利用アプリを閉じる。
2. 初回は**Connect**を押し、ブラウザのポート選択画面でRedPointを選ぶ。このユーザー操作でUSB deviceへのpermissionを付与する。
3. GETの応答後、**Connected**となり、設定controlが有効になる。
4. sliderで感度、checkboxで反転を変更する。操作値と「デバイス確認値」は別表示。
5. 永続化するには**Save**を押す。未送信SETの反映後、SAVEの成功応答で**Saved**になる。
6. **Reset to defaults**はRAMのみdefaultへ戻す。defaultも保存する場合は続けて**Save**を押す。
7. 終了時は**Disconnect**を押す。

#### 許可済みRedPointへの自動接続

2回目以降はpage load時に`navigator.serial.getPorts()`で、このoriginへ許可済みのportだけを取得します。
`getInfo().usbVendorId === 0x2E8A`で候補を絞り（PIDは固定しない）、GETの正常応答とconfigの型・範囲を検証します。
許可取得の`requestPort()`はConnectクリック時だけで、page load時にはpickerを開きません。
ブラウザのpermissionを削除した場合や別originでは、再度Connectから許可が必要です。
localhostとGitHub Pagesの許可は別です。
[Web Serialの公式説明](https://developer.chrome.com/docs/capabilities/serial)も参照してください。

- 1候補: open → GET同期・本人確認 → 接続を維持。不要なclose／再openはしません。
- 複数候補: 1台ずつopen → GET → close。RedPointが1台ならそのportを再openしてGET同期します。
  0台ならDisconnected、2台以上なら自動選択せずConnectでの手動選択を案内します。
- 初回GETのtimeoutは通常接続／1候補で2秒、複数候補のprobeで各1.2秒です。
  初回検証失敗ではportを閉じ、Connectから再試行できます。探索中はConnectを無効化します。
- GET成功後にのみ既存のPING capability probeとheartbeatを開始します。
  旧firmwareのUNKNOWN_COMMANDではheartbeatだけを無効化し、設定機能は継続します。
- USBのconnect eventでも、sessionがなく接続・探索・切断中でなければ許可済みportを再検出します。
  **手動Disconnect後は同じpage session内で自動再接続しません**。手動Connectは使用でき、reload後は再び自動検出します。

手動pickerはVID情報がないportも選べるよう従来どおり表示しますが、GETによる本人確認は必須です。
探索失敗はページ全体のfatal errorにはしません。probe後のclose失敗時は次候補を開かず中止します。
reader／writer lockを解放してからcloseし、再接続できない場合はUSBを挿し直してください。
Serial探索はGETだけを使用します。起動時のHTTP probeはsame-originだけで、外部backend／CDN／device情報の永続化は追加しません。

制限: GET protocolが同一の別firmwareは識別できません。ブラウザ・OSによるport open／close時間はGET timeoutに含まれません。
起動途中やport占有中は検出に失敗することがあります。切断処理中のUSB connect eventは処理を重ねず無視するため、
素早い抜き差しで復帰しなければConnectを使ってください。

GitHub Pagesでの実機確認（変更の公開後）:

1. HTTPSのPages URLを開き、初回はpickerが自動表示されず、Connectで許可・接続できることを確認。
2. reloadし、pickerなしでConnectedとなり、GETの設定値と通常のSET／RESET／SAVE／shortcutが使えることを確認。
3. 手動Disconnect後は抜き差ししても再接続せず、Connectまたはreloadで接続できることを確認。
4. 手動Disconnectしていない状態でUSBを抜き差しし、自動復帰とheartbeat再開を確認。
5. 許可済みRedPointを2台接続し、自動選択されず、Connectで選べることを確認。
6. permission削除後はConnectが再び必要なこと、Serial Monitor使用中は失敗後も手動再試行できることを確認。

感度の範囲は0～10、sliderの刻みは0.01です。0はそのモードのpointer移動を停止します。
Middle sensitivityは通常感度と乗算せず、logical Mouse Middle押下中に直接選択される倍率です。
defaultは通常1.00、Middle 0.40、反転なしです。


### ボタン割当とShortcut Recorder

Buttonsの各物理ボタンでLeft / Middle / Right ClickまたはDisabledを選択できます。
Keyboardは**Record Shortcut**を押し、実際のキーを入力します。modifierの押下状態を表示し、
最初のnon-modifier keyで記録を完了してSETします。操作値とデバイス確認値は分けて表示し、
デバイス応答後に確定します。永続化には明示的なSaveが必要です。

- Ctrl / Shift / Alt / Metaと1キー、または修飾なし1キーに対応します。例: A、Escape、F5、Ctrl+Shift+T、Alt+Left。
- EscapeとBackspaceも割当可能です。取消は**Cancel**、解除は**Disabled**を使います。
- 記録中だけkeydown/keyupを捕捉し、repeatを無視します。通常時のページのキー操作は横取りしません。
- 未対応キーは既存割当を保持してエラー表示します。Cancel、フォーカス喪失、ページ非表示、切断で記録を終了します。
- `KeyboardEvent.code`からHID usageへ変換します。英数字、標準記号、F1～F24、navigation/arrows、numpad、IntlBackslash、ContextMenu等に対応します。対応表は[shortcuts.js](../../configurator/shortcuts.js)にあります。
- キー位置を保存するため、実際の文字はOS側のkeyboard layoutに依存します。記号の表示名はUS配列基準です。左右modifierは区別しません。
- JIS固有キー、IME入力、AltGraph、media/Fn、modifierだけの割当は未対応です。Alt+Tab、Win+L、Ctrl+Alt+DeleteなどOS/browserが先に処理するキーは記録できない場合があります。

押下時のActionを保持してreleaseするため、押下中に設定を変えても旧Actionを正しく解放します。
同じMouse/key/modifierを複数ボタンが共有した場合は最後のreleaseまで保持します。
異なるショートカットを同時に押すとmodifierは合成されます。長押しのkey repeatはOSに従います。
Middle感度は物理ボタン位置ではなく、現在heldのMouse Middle Actionに従います。
旧4項目firmwareではPointer設定を使えますが、Buttonsはfirmware更新案内とともに無効になります。

## EN

### Wheel sensitivity curve

Horizontal / Vertical Wheel sliders use a logarithmic curve for fine low-sensitivity adjustment. The leftmost position is OFF (0), the next is 0.001×, and each 100 steps spans a decade: 0.01× → 0.1× → 1× → 10×. Displayed and transmitted values are actual multipliers, retained to six decimal places. Pointer remains linear from 0 to 10 in 0.01 steps.

Existing settings, the 0.40× default and storage format are unchanged. Loaded values are not rounded and rewritten until edited. Wheel output still consists of integer ticks: lower sensitivity reduces tick frequency, while the OS/application determines the scrolling distance of each tick.


The current Pico SDK device serves its Configurator at `http://169.254.7.1/`. [USB Ethernet](../usb-ethernet.md) is authoritative for HTTP-first/Serial fallback; [protocol](../protocol.md) defines the schema. C.3 has Pointer / Wheel / Buttons sections; choose Keyboard Shortcut in a row to reveal Record. See [C.2](../../firmware/redpoint_pico/MILESTONE_C2.md) and [C.3](../../firmware/redpoint_pico/MILESTONE_C3.md).

The detailed material below was preserved from the earlier README. Statements that integration remains unfinished, references to Middle sensitivity, support for old four-field firmware, and separately visible confirmed values describe **earlier revisions**. Current Pico firmware is integrated, separates Pointer/Wheel settings, and uses Middle as a native Wheel/Pan modifier. The current UI requires C.2 schema; some confirmed values remain available to screen readers. See the protocol for old-Pages aliases and new-UI/old-firmware limitations.

Run commands at repository root unless stated otherwise.
[Development index](README.md) · [Project overview](../../README.md)

### Starting the Configurator

`configurator/` is plain HTML/CSS/JavaScript: no framework, npm install, frontend build, backend processing, external API, or CDN is needed for the static frontend. On startup it POSTs the GET command to same-origin `api/command`. A valid RedPoint response selects **USB Ethernet HTTP mode** automatically; otherwise it falls back to **Web Serial API**.
HTTP needs neither a permission dialog nor a secure context and works on devices such as iPad without Web Serial. Disconnect ends a logical session; Connect repeats GET synchronization. The frontend does not hard-code the device IP.

The current Pico SDK target integrates the lwIP HTTP adapter, static generator, Ethernet, HID, Flash and LED (C.2). The Arduino sketch alone does not enable Ethernet. See [USB Ethernet integration](../../docs/usb-ethernet.md) for architecture, CMake integration, and hardware checks.

Serve local static files from the repo root:

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory configurator
```

Open [http://localhost:8000](http://localhost:8000) in desktop Chrome/Edge with Web Serial support. Web Serial requires a secure context (localhost or HTTPS). The UI detects support/context and explains unavailable functionality. This simple HTTP server only serves files; it receives no device settings or Serial data. Stop it with Ctrl+C.

1. Connect RedPoint over USB and close Serial Monitor/other port users.
2. On first use click **Connect**, then choose RedPoint in the browser picker to grant permission.
3. After GET succeeds, **Connected** appears and controls become available.
4. Change sensitivity/inversion. Draft and confirmed device values are distinct (some confirmation text is screen-reader-only in C.3).
5. Click **Save** for persistence. Pending SETs finish before SAVE; **Saved** requires a successful device reply.
6. **Reset to defaults** (now **Reset**) resets RAM only. Follow it with Save to persist defaults.
7. Click **Disconnect** when finished.

#### Auto-connect to authorized RedPoints

On subsequent loads, `navigator.serial.getPorts()` returns ports already authorized for this origin. Candidates are filtered by `getInfo().usbVendorId === 0x2E8A` without a fixed PID, then identified by a valid GET response with typed/in-range configuration.
`requestPort()` runs only after Connect, never on page load. Clearing permission or using another origin requires permission again; localhost and GitHub Pages permissions are separate. See [Chrome's Web Serial documentation](https://developer.chrome.com/docs/capabilities/serial).

- One candidate: open → GET/identify → keep the connection; no unnecessary close/reopen.
- Multiple candidates: open → GET → close each in turn. Reopen and GET the unique match. With zero matches stay disconnected; with two or more require manual selection.
- Initial GET timeout is two seconds for normal/single-candidate connections, 1.2 seconds per multi-candidate probe. Failure closes the port and permits manual retry. Connect is disabled during discovery.
- Only a successful GET starts the PING capability probe and heartbeat. UNKNOWN_COMMAND from older firmware disables heartbeat alone; configuration remains usable if its schema is compatible.
- USB connect events rescan authorized ports only when no session/connection/discovery/disconnection is active. **Manual Disconnect suppresses auto-reconnect for that page session**; manual Connect still works, and reload restores discovery.

The manual picker also permits ports lacking VID information, but GET identification remains mandatory. Discovery failures are not fatal page errors. A failed probe close stops discovery rather than opening another port. Reader/writer locks are released before closing; replug USB if reconnection fails.
Serial discovery uses GET only; HTTP probes use only the page origin. No external backend, CDN, or persisted device information is added.

Limitations: identical GET protocols cannot establish unique firmware identity. OS open/close delays are outside GET deadlines. Booting or busy ports may fail identification. USB connect events during disconnect are ignored rather than overlapping teardown; use Connect if a rapid replug does not recover.

GitHub Pages hardware checks after publication:

1. Open HTTPS Pages; no automatic picker on first use, but Connect grants permission and connects.
2. Reload: connect without a picker; verify GET values and SET/RESET/SAVE/shortcuts.
3. After manual Disconnect, replug must not auto-connect; Connect or reload should work.
4. Without manual Disconnect, replug and verify auto-recovery/heartbeat restart.
5. Authorize two RedPoints: require manual choice instead of auto-selection.
6. Clear permission and verify Connect is required again. With Serial Monitor holding the port, failure must still allow manual retry.

Historical sensitivity semantics: range 0–10, slider step 0.01; zero stopped movement in the selected mode. Middle sensitivity was selected directly while a logical Mouse Middle action was held, rather than multiplied by normal sensitivity. Defaults were normal 1.00, Middle 0.40, no inversion. Current independent Wheel settings are specified in the protocol.

### Button mapping and Shortcut Recorder

Each physical button can select Left/Middle/Right Mouse action or Disabled. Choose Keyboard Shortcut, reveal Record, then press the desired key. Modifier state is previewed; the first non-modifier finishes recording and sends SET. Device responses confirm the assignment; Save is still explicit.

- Ctrl / Shift / Alt / Meta plus one key, or one unmodified key: e.g. A, Escape, F5, Ctrl+Shift+T, Alt+Left.
- Escape/Backspace are assignable. Use Cancel to cancel and Disabled to clear.
- Keydown/keyup are captured only during recording; repeats are ignored. Normal page keys are not intercepted.
- Unsupported keys retain the existing binding and show an error. Cancel, focus loss, page hiding, or disconnect ends recording.
- `KeyboardEvent.code` maps to HID usage. Letters/digits, standard punctuation, F1–F24, navigation/arrows, numpad, IntlBackslash, and ContextMenu are supported. See [shortcuts.js](../../configurator/shortcuts.js).
- Key positions are saved; actual characters depend on the OS layout. Symbol labels use US layout; left/right modifiers are not distinguished.
- JIS-specific keys, IME, AltGraph, media/Fn, and modifier-only bindings are unsupported. OS/browser shortcuts such as Alt+Tab, Win+L, or Ctrl+Alt+Delete may be intercepted before recording.

Actions latch on press and release the latched action even if settings change while held. Shared mouse/key/modifier owners retain the action until the last release. Simultaneous shortcuts combine modifiers; held-key repeat is OS-controlled. Middle behavior follows the logical Mouse Middle owner, not the physical button position.
Historical pre-C.2 UI supported Pointer controls on old four-field firmware while disabling Buttons with an update message; current C.2-schema requirements supersede that behavior.
