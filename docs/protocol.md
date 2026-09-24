# RedPoint Serial configuration protocol

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

同じcommand／`@CONFIG` framingはUSB Ethernetの`POST /api/command`でも使用します。
validation・mutation・SAVE・LED更新・response生成は`config_command.cpp`が共通のsource of truthです。
HTTP transport、body上限、lwIP組込については[USB Ethernet integration](usb-ethernet.md)を参照してください。

### 接続とフレーミング

USB CDC Serialを使用する。接続設定は115200 baud、8N1、flow controlなし。
HID Mouse・KeyboardとWebSerial設定通信は同時に動作する。明示的なSAVEで設定をFlashへ保存する。
Arduino Philhower RP2040 core 6.1.0のMouse / KeyboardライブラリとPico SDK USB stackを使う。
`firmware/redpoint/`をスケッチとして開き、同じフォルダの全`.cpp`をビルドする。
ボードとUSB設定は実機で使用している設定を維持する。

要求はASCIIの1行1コマンド。LF、CRLF、CRを受け付ける。
行末を除き最大95 bytes。スペースとTABはトークン区切りで、前後・連続も許可する。
空行・空白だけの行は無視する。コマンド名とキーは大文字小文字を区別する。
その他の制御文字、非ASCII、長すぎる行は行末まで破棄し、1回エラーを返す。
途中まで届いた行は次回の受信へ保持し、タイムアウトは設けない。
不完全な送信から復帰したいクライアントは、改行で区切ってからGETする。

応答は1行JSONで、必ず`@CONFIG `で始まる。末尾はCRLF。
既存の起動・pointer・buttonログは`@DEBUG `で始まる。
ブラウザは受信chunkを行バッファへ蓄積し、改行単位で分解した後、
`@CONFIG `を除いた部分だけJSONとして解析する。`@DEBUG `はログとして扱う。
chunkの境界は行の境界とは限らず、応答の前後にdebug行が入ることがある。
どちらもmain loop/setupから出力し、ISRからSerialは出力しない。

要求IDはないため、クライアントは1コマンドを送って応答を待ってから次を送る。
待機時間の目安は2秒。timeoutは失敗確定ではないためGETで状態を再取得する。
連続送信はUSB受信バッファの容量を超える可能性があるため避ける。
受信処理は1 loopあたり最大32 bytes、最大1非空行で、改行を待ってブロックしない。
ただしSerial出力自体の待ち時間はUSB coreに依存する。高頻度debug時は実機検証が必要。
`DEBUG_INPUTS`を0にしても設定通信は動作する。

### 設定

| キー | default | SETで受け付ける値 | 意味 |
| --- | --- | --- | --- |
| pointerSensitivity | 1.00 | 0～10（両端を含む） | logical Mouse Middleがheldでないときの倍率 |
| pointerInvertX / pointerInvertY | false | `0` / `1` | Pointerの各軸だけを反転 |
| wheelSensitivityX / wheelSensitivityY | 0.40 | 0～10（両端を含む） | Pan / Wheelそれぞれの感度。0でその軸を無効化 |
| wheelInvertX / wheelInvertY | false | `0` / `1` | Wheelの各軸だけを反転 |
| leftAction | `mouse:left` | 下記Action文字列 | Left物理ボタンの割当 |
| middleAction | `mouse:middle` | 下記Action文字列 | Middle物理ボタンの割当 |
| rightAction | `mouse:right` | 下記Action文字列 | Right物理ボタンの割当 |

感度は数字と小数点1個までの非負10進表記。`1`、`0.35`、`.5`、`1.`は有効。
符号、指数表記、NaN、Infinity、16進表記、単位付き文字列は無効。
floatで保持し、GETでは小数点以下6桁を返すため丸めがある。
boolの応答はJSON booleanだが、SETの入力は`0`/`1`に限定する。
Pico SDK C.2ではlogical MiddleがheldでないときPointer、heldのときWheelを選択する。
logical X=PS/2 Y、logical Y=PS/2 Xと±127 clampは従来どおり。
PointerだけにpointerInvertX/YとpointerSensitivityを適用する。
Wheelはlogical X→Pan、logical Y→Wheelへ独立感度・Invertを適用し、Mouse X/YとMiddle pressを送らない。
Wheelのbase polarityはXを維持、Yだけ反転。Invert OFFでstick方向へviewが動く基準とする。
Pointer/Wheelは別fraction stateを持ち、mode切替時（packetがない場合も）にclearする。

旧Pages互換のSET alias: `middleSensitivity`はWheel両軸を同じ値へ設定し、`invertX/Y`はPointerのみ更新。
GET/SET/RESET/SAVEの応答にも旧名を追加し、middleSensitivityはWheel Yの代表値を返す。
内部state/保存形式に旧fieldは持たない。旧UIでは独立Wheel X/YやWheel Invertを操作できない。
新UIはC.2 schemaを要求するため、C.1以前のfirmwareと使う場合は旧Pagesを使用する。
Arduino sketchは歴史的なMiddle-held X/Y経路の参照実装であり、native WheelはPico SDK targetの機能。

出力は再度±127に飽和させる。飽和した整数分は捨て、小数部のみ持ち越す。
SET/RESET成功時は全残量を消し、次のpacketから設定を適用する。
同じ値のSETでも残量は消す。debounceとPS/2受信設定は変更しない。

#### Button Action

Actionは`mouse:left`、`mouse:middle`、`mouse:right`、`disabled`、または
`key:MM:KK`の1 token。MMはmodifier bitfield、KKはKeyboard page (0x07)のHID Usage ID。
各hexは必ず2桁で、入力は大小どちらも許可し、応答はuppercaseに正規化する。
modifierはCtrl=01、Shift=02、Alt=04、GUI/Meta=08で、上位4 bitは0を要求する。
例: `key:03:17`はCtrl+Shift+T、`key:00:04`はA。

対応usageは04～73のうち32 (Non-US #)と66 (Power)を除く。
英数字、標準punctuation、Enter/Escape/Backspace/Tab/Space、F1～F24、
navigation/arrows、CapsLock/ScrollLock/PrintScreen/Pause、NumLockとnumpad、
IntlBackslash、ContextMenuに対応する。完全なcode/usage/表示名対応表は
`configurator/shortcuts.js`。JIS固有のIntlYen/IntlRo、IME、AltGraph、media/Fnは対象外。
modifier-only、usage 00やE0～E7、reserved modifier、未知type/mouse名、
不正hex、桁数・末尾dataの不一致は`INVALID_VALUE`。空白による余分なtokenは`INVALID_ARGUMENTS`。

内部・Flash表現はtype/code/modifiers各1 byte。
typeはDisabled=0、MouseButton=1、KeyboardShortcut=2。
Mouse codeはLeft=1、Middle=2、Right=3（Mouseライブラリのbitmaskとは別）でmodifiers=0。
Disabledはcode/modifiersとも0を要求する。Keyboardは上記usageとmodifierを使用する。

debounce済みDOWNでActionをlatchし、UPでそのActionをreleaseする。
押下中のSET/RESETは次回DOWNへ反映し、現在heldのActionは変更しない。
Mouse/key/modifierごとの所有数を管理し、最後の所有者のUPでのみreleaseする。
通常のreleaseで`Keyboard.releaseAll()`は使用しない。
Middle倍率はこのlatchされたlogical Mouse Middleの所有状態から選ぶため、
RightをMouse Middleへ割り当てた場合も適用される。MiddleをKeyboardへ変えると適用されない。

core 6.1.0のKeyboard APIはraw usageを直接受けないため、`keyboard_mapping.cpp`で
usage+136へ変換し、modifierは`KEY_LEFT_CTRL/SHIFT/ALT/GUI`で送る。
右modifierも左modifierへ統一する。文字列/ASCII変換はしない。
複数ボタンの同時押下では通常のKeyboard同様にmodifierが合成され、
最大3個のnon-modifier keyをheldにする。押し続けた際のrepeatはOSに従う。
USB初期化後にも押されている起動時のボタンは、その時点でActionをpressする。

### コマンドと応答

#### GET

現在のRAM設定を取得する。引数なし。

```text
GET
@CONFIG {"ok":true,"command":"GET","config":{"pointerSensitivity":1.0,"middleSensitivity":0.4,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.4,"wheelSensitivityY":0.4,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
```

#### SET <key> <value>

1項目をRAMへ即時適用する。成功時は変更後の全設定を返す。
検証に失敗した場合、設定と移動残量は変更しない。

```text
SET middleSensitivity 0.25
@CONFIG {"ok":true,"command":"SET","config":{"pointerSensitivity":1.0,"middleSensitivity":0.25,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.25,"wheelSensitivityY":0.25,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
SET pointerSensitivity 1.10
SET invertX 1
SET invertY 0
```

最後の3例もそれぞれ送信後に応答を待つ。Actionも同じSETを使う。

```text
SET rightAction key:03:17
SET middleAction disabled
SET leftAction mouse:right
```

以下の2段落はC.2以前の互換性仕様の履歴です。現行schemaは上記C.2仕様を参照してください。

成功応答のconfigには常に7項目を含む。旧Configuratorは追加fieldを無視できる。
新ConfiguratorはAction fieldが3つともない旧4項目応答も許可し、Pointerを利用可能に保つ。
この場合Buttonsは「Firmware update required for button mapping」で無効化し、Action SETを送らない。
一部だけAction fieldがある応答や不正Actionは設定として受理しない。

#### RESET

Pointerと3つのButton Actionを含むRAM設定をdefaultに戻す。引数なし。再起動やFlash操作はしない。
成功応答はGETと同じ構造で`command`が`RESET`になる。
Flashの保存内容は変更しない。RESET後にSAVEしなければ、再起動後は以前保存した値へ戻る。
defaultを永続化するにはRESET成功後にSAVEする。

#### SAVE

引数なし。現在のRAM上DeviceConfigを検証してFlashへ保存する。
保存後のFlash再読込・内容一致・CRC/値検証まで成功したときだけ、以下を返す。
応答のconfigは保存が確認できた値。RAM設定と移動の小数残量は変更しない。
Flash上のレコードと同一の場合はwrite/eraseを省略して成功を返す。

```text
SAVE
@CONFIG {"ok":true,"command":"SAVE","config":{"pointerSensitivity":1.0,"middleSensitivity":0.25,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.25,"wheelSensitivityY":0.25,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
```

保存対象の検証失敗と、保存／照合の失敗を区別する。

```text
@CONFIG {"ok":false,"error":"INVALID_CONFIG"}
@CONFIG {"ok":false,"error":"SAVE_FAILED"}
```

エラー時もRAM設定は変えない。保存失敗時のFlash内容は保証しない。
成功応答を受信していない場合、hostはSavedと表示してはいけない。
timeout時はGETでRAM設定を再取得するが、GETはFlash保存成功の証明にはならない。
遅延したSAVE応答を再同期GETの応答として使用せず、必要なら再同期後にSAVEを再実行する。
旧firmwareの`NOT_IMPLEMENTED`も通常のデバイスエラーとして扱える。

SETのたびにSAVEしない。ユーザーの明示操作時だけ送信する。
Configuratorは未送信SETを反映してからSAVEし、その間は設定操作を無効化する。
再接続時にはGETで現在値を取得し、保存状態は未確認とする（GETはRAMのみを返す）。

### Flash保存形式と起動

Pico SDK版は16 MiB Flash末尾の既存sector `0x10FFF000–0x10FFFFFF`、offset 0へ44-byte v3を保存する。
sector配置、erase/program、readback照合、explicit SAVE方針はC.1と同じ。
Arduino版はPhilhower RP2040 core 6.1.0標準EEPROM emulationで同じrecord codecを使用する。
filesystemは不要。多byte値はlittle endian、floatはIEEE-754 binary32。
C++ structのpaddingやboolのメモリ表現には依存しない。

| Offset | Bytes | 内容 |
| --- | --- | --- |
| 0 | 4 | magic: ASCII `RPNT`（uint32 0x544E5052） |
| 4 | 2 | format version: 3 |
| 6 | 2 | record length: 44 |
| 8 | 4 | pointerSensitivity |
| 12 | 4 | wheelSensitivityX |
| 16 | 1 | pointerInvertX: 0 / 1 |
| 17 | 1 | pointerInvertY: 0 / 1 |
| 18 | 2 | reserved: 0 |
| 20 / 24 / 28 | 各1 | Left / Middle / Right action type |
| 21 / 25 / 29 | 各1 | action code |
| 22 / 26 / 30 | 各1 | modifiers |
| 23 / 27 / 31 | 各1 | reserved: 0 |
| 32 | 4 | wheelSensitivityY |
| 36 / 37 | 各1 | wheelInvertX / wheelInvertY: 0 / 1 |
| 38 | 2 | reserved: 0 |
| 40 | 4 | bytes 0～39のCRC-32/ISO-HDLC |

CRCはreflected polynomial 0xEDB88320、初期値0xFFFFFFFF、最終XOR 0xFFFFFFFF。
拡張時はformat versionとrecord lengthを更新し、必要に応じて移行処理を追加する。
v1 (24 bytes)とv2 (36 bytes)も読み込む。旧offset 8/12はpointerSensitivity/middleSensitivity、
16/17はinvertX/Y、18/19はreserved。v1のCRCはoffset 20、v2はoffset 32。
v2のactionsはoffset 20～31でv3と同じ。v1はdefault actionsを追加する。
旧Pointer値はそのまま新Pointerへ、旧middleSensitivityはWheel X/Yの両方へ移行する。
Wheel Invertは両方falseで初期化し、旧Pointer Invertを引き継がない。
CRC・値・reservedを検証してRAM上だけで移行する。次の明示SAVEでv3へ書く。
未知versionや不正recordは全defaultへfallbackする。

setupのPS/2割り込み・HID初期化前に1回loadする。
magic、version、長さ、CRC、reserved、bool表現、感度の有限性・0～10の範囲、Actionのtype/code/modifiersをすべて検証する。
未保存（消去済み領域を含む）またはどれか不正なら全設定をdefaultに戻す。
bootやfallback、SET、RESETではFlashを書かず、壊れたデータの自動修復もしない。

EEPROM.beginはcore内部で256-byteのRAMバッファを起動時に確保する。
SAVE時だけcommitを呼び、同じサイズでbeginし直してFlashを再読込する（再確保なし）。
EEPROM.endは暗黙のcommitがあるため使用しない。
単一sectorをerase/programするので、保存中の電源断で旧設定が失われる可能性がある。
次回起動はCRCなどで検証し、不正ならdefaultを使用する。二重化やwear levelingは未実装。

commit中はcoreが割り込みを止めるため、SAVE時だけHID/PS/2入力が短時間停止し、
移動やボタン変化を取りこぼす可能性がある。SAVEは静止中に行い、完了まで電源を維持する。
main loopのSAVE後に未処理FIFO・途中frame/packetを破棄し、次のpacket gapで再同期する。
ISR、通常のdecode、pointer変換、debounceは変更しない。

### エラー

すべて`@CONFIG {"ok":false,"error":"CODE"}`形式。
エラー応答には元の入力を含めない。クライアントは送信中の1要求と対応させる。

| CODE | 意味 |
| --- | --- |
| UNKNOWN_COMMAND | 未知のコマンド |
| INVALID_ARGUMENTS | 引数不足・余分な引数 |
| UNKNOWN_KEY | SETの未知のキー |
| INVALID_VALUE | キーに対して無効な値・範囲外 |
| INVALID_LINE | 行が長すぎる、または無効な文字を含む |
| INVALID_CONFIG | SAVE対象のRAM設定が不正（Flashを書かない） |
| SAVE_FAILED | Flash保存または再読込検証に失敗 |
| NOT_IMPLEMENTED | 旧firmwareのSAVE未対応応答（現firmwareでは返さない） |

### 実機確認と次phase

- defaultで移動方向、低速・高速移動、L/M/R press/release、drag、Middle時0.40倍をbaselineと比較する。
- 小さな連続移動、正負の反転、Middle切替で小数残量の挙動を比較する。
- GET、SET middleSensitivity 0.25、SET pointerSensitivity 1.10、両軸反転、RESETを確認する。
- 0と10の感度、大きな移動でもwrapしないこと、ボタンが独立して動くことを確認する。
- 分割送信、CR/LF/CRLF、空行、無効値、未知キー、引数過不足、長すぎる行の後にGETで復帰することを確認する。
- SET→SAVE成功→電源再投入で保存値へ戻ることを確認する。
- SETのみ／RESETのみではFlashが変わらず、再起動後は保存値へ戻ることを確認する。
- RESET→SAVE→再起動でdefaultへ戻り、未保存・不正レコード時もdefaultへfallbackすることを確認する。
- SAVE直後のpacket再同期、移動・ボタン・debugの復帰を確認する。
- DEBUG_INPUTSの0/1、debug中の応答JSON、Serial未接続・切断・再接続中のHID、連続移動中のdrop値を確認する。

Button・migrationの実機acceptance手順は[開発・実機検証](development/validation.md#ボタン割当の実機確認)を参照。
次phase候補は保存中の電源断に対する二重化、実機での長時間検証。
将来WebHIDへ移行する場合は設定用HID reportとtransportを別途設計する。
Mouse reportは既存のまま、KeyboardライブラリのHID descriptorを追加している。

### PING / Device status heartbeat

要求:

```text
PING
```

応答（configは不要）:

```text
@CONFIG {"ok":true,"command":"PING"}
```

引数は禁止で、余分な引数には既存の`INVALID_ARGUMENTS`を返します。
PINGも通常の1要求→1応答です。未応答の要求と並行送信しません。
成功したSETと、構文が有効なGET／RESET／SAVE／PINGはfirmwareのactivityを更新します。
SAVEの保存失敗も有効な要求としてactivityを更新し、同時にErrorを表示します。
不明コマンド、無効なSET、parser errorはactivityを更新せず、Error表示の対象です。
firmwareは最後のactivityから6000 ms未満をconnectedとして扱い、unsigned差分でmillis rolloverに対応します。
Serial portがopenなだけではactiveと判定しません。

ブラウザは接続ごとにGET同期を成功させてから、最初のidle機会にPINGを1回probeします。
成功すると2000 ms間隔でPINGします。`UNKNOWN_COMMAND`ならそのsessionのtimerを停止し、
ユーザーへのfatal error表示なしでPointer／Button設定を継続します。旧形式のconfig応答も引き続き受理します。
再接続ではcapabilityを破棄して再probeします。
probeやheartbeatの通信timeoutは既存のGET再同期経路、write失敗は切断経路を使います。

ユーザーのdraft（debounce待ちを含む）、SET／RESET／SAVE要求、GET同期、shortcut記録中はPINGをskipします。
PINGをqueueに蓄積しません。すでに送信済みのPINGは応答を受けるまで直列化を維持し、
応答直後に待機中のユーザー操作を優先します。Disconnect、入力stream終了、USB抜去でtimerを停止します。
command名が一致しない成功応答は待機要求を完了させません。

LEDはfirmware側の状態から決定し、C.2でもLED状態の意味と優先順位は変更しません。
全`@CONFIG` error応答でREDを2秒保持し、新しいエラーで延長します。
SAVE直前にPURPLEを送信し、完了後もLEDだけ開始時刻から最低150 ms保持します。
ErrorはPURPLEより優先されます。Flash／protocol／HIDを表示時間のために待たせません。
NeoPixelのハードウェアラッチ期間中はSerial pollを次loopへ譲り、blocking waitを回避します。

## EN

The same commands and `@CONFIG` framing are also used by USB Ethernet `POST /api/command`. `config_command.cpp` is the shared source of truth for validation, mutation, SAVE, LED updates, and response generation. See [USB Ethernet integration](usb-ethernet.md) for transport limits and lwIP integration.

### Connection and framing

USB CDC Serial uses 115200 baud, 8N1, no flow control. HID Mouse/Keyboard and Web Serial configuration operate concurrently; explicit SAVE persists settings.
The Arduino reference uses Philhower RP2040 core 6.1.0 Mouse/Keyboard libraries with its Pico SDK USB stack. Open `firmware/redpoint/` as a sketch and compile all sibling `.cpp` files, retaining the hardware-verified board/USB settings.

Requests are ASCII, one command per line, terminated by LF, CRLF, or CR. Maximum content excluding line endings is 95 bytes. Spaces/tabs delimit tokens; leading/trailing/repeated whitespace is permitted. Blank/whitespace-only lines are ignored. Commands/keys are case-sensitive. Other control characters, non-ASCII, or excessive length discard the rest of the line and produce one error. Partial lines persist across receives without a firmware line timeout. Recover incomplete sends by sending a newline before GET.

Responses are one-line JSON prefixed with `@CONFIG ` and ending CRLF. Existing boot/pointer/button logs start with `@DEBUG `. Clients buffer chunks, split complete lines, and parse JSON only after the config prefix. Debug can occur before/after replies; chunk boundaries are not line boundaries. Output occurs from setup/main loop, never the ISR.

There are no request IDs: send one command and await its response before another. A typical deadline is two seconds; timeout does not prove failure, so read state with GET. Avoid bursts exceeding USB RX buffers. Receive processing is bounded to 32 bytes and one nonempty line per loop without waiting for a newline. Serial output blocking depends on the USB core; debug-heavy operation needs hardware checks. Configuration still works with `DEBUG_INPUTS=0`.

### Configuration

| Key | Default | SET values | Meaning |
| --- | --- | --- | --- |
| pointerSensitivity | 1.00 | 0–10 inclusive | Multiplier without a held logical Middle action |
| pointerInvertX / pointerInvertY | false | `0` / `1` | Pointer-only axis inversion |
| wheelSensitivityX / wheelSensitivityY | 0.40 | 0–10 inclusive | Independent Pan / Wheel multipliers; zero disables that axis |
| wheelInvertX / wheelInvertY | false | `0` / `1` | Wheel-only axis inversion |
| leftAction | `mouse:left` | Action token below | Physical Left binding |
| middleAction | `mouse:middle` | Action token below | Physical Middle binding |
| rightAction | `mouse:right` | Action token below | Physical Right binding |

Sensitivity accepts unsigned nonnegative decimal digits with at most one dot: `1`, `0.35`, `.5`, `1.` are valid. Signs, exponents, NaN/Infinity, hex, and suffixes are invalid. Values use float; GET rounds to six fractional decimal places. JSON replies use booleans, while SET requires 0/1.
Pico C.2 selects Pointer without a logical Middle owner, Wheel with one. The existing logical X=PS/2 Y, logical Y=PS/2 X mapping and ±127 clamp remain. Pointer alone uses pointerSensitivity/pointerInvertX/Y. Wheel independently maps logical X→Pan and logical Y→Wheel, suppressing Mouse X/Y and the Middle press. Base polarity preserves X and reverses Y; Invert OFF means moving the view in the stick direction. Pointer/Wheel have separate fractional state, cleared on mode transitions even without packets.

Legacy Pages aliases: SET middleSensitivity sets both Wheel axes; SET invertX/Y changes Pointer only. GET/SET/RESET/SAVE also include legacy projection fields; middleSensitivity reports Wheel Y. No legacy fields own internal or persistent state. Old UI cannot edit independent Wheel axes/inversion. The new UI requires C.2 schema; use old Pages with C.1-or-earlier firmware. The Arduino sketch retains historical Middle-held X/Y behavior; native Wheel is a Pico target feature.

Clamp final output again to ±127, discard saturated integer excess, retain fractions. Successful SET/RESET clears all remainders for the next packet, even when SET repeats a value. PS/2 reception and debounce are unchanged.

#### Button Action

One token: `mouse:left`, `mouse:middle`, `mouse:right`, `disabled`, or `key:MM:KK`. MM is a modifier bitfield and KK a Keyboard page (0x07) HID Usage ID, each exactly two hex digits. Input is case-insensitive hex; output is uppercase. Ctrl=01, Shift=02, Alt=04, GUI/Meta=08; upper four bits must be zero. `key:03:17` is Ctrl+Shift+T; `key:00:04` is A.
Supported usages are 04–73 excluding 32 (Non-US #) and 66 (Power): alphanumerics, standard punctuation, Enter/Escape/Backspace/Tab/Space, F1–F24, navigation/arrows, CapsLock/ScrollLock/PrintScreen/Pause, NumLock/numpad, IntlBackslash and ContextMenu. Full mappings are in `configurator/shortcuts.js`. IntlYen/IntlRo, IME, AltGraph, media/Fn are unsupported.
Modifier-only, usage 00 or E0–E7, reserved modifiers, unknown type/mouse names, invalid hex/length/trailing data produce INVALID_VALUE. Extra whitespace-separated tokens produce INVALID_ARGUMENTS.

Internal/Flash action fields are one byte each: type/code/modifiers. Disabled=0, MouseButton=1, KeyboardShortcut=2. Mouse codes Left=1/Middle=2/Right=3 are not library bitmasks and require zero modifiers. Disabled requires zero code/modifiers. Keyboard uses the usages/modifiers above.
Debounced DOWN latches the action; UP releases that same action. SET/RESET while held affects only the next DOWN. Mouse/key/modifier owner counts release only on the last UP; ordinary releases do not call Keyboard.releaseAll(). Logical Middle ownership selects Middle behavior, so mapping Right to Middle enables it; mapping physical Middle to Keyboard does not.
The core 6.1.0 Arduino Keyboard API requires usage+136 via `keyboard_mapping.cpp`, with KEY_LEFT_CTRL/SHIFT/ALT/GUI for modifiers. Right modifiers normalize to left; no ASCII/string conversion occurs. Simultaneous actions combine modifiers and hold at most three non-modifier keys; repeats are OS-controlled. Buttons still held after USB initialization press their startup action.

### Commands and responses

#### GET

Returns current RAM settings; no arguments.

```text
GET
@CONFIG {"ok":true,"command":"GET","config":{"pointerSensitivity":1.0,"middleSensitivity":0.4,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.4,"wheelSensitivityY":0.4,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
```

#### SET <key> <value>

Applies one field immediately in RAM and returns the complete resulting config. Validation failure changes neither config nor motion remainders.

```text
SET middleSensitivity 0.25
@CONFIG {"ok":true,"command":"SET","config":{"pointerSensitivity":1.0,"middleSensitivity":0.25,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.25,"wheelSensitivityY":0.25,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
SET pointerSensitivity 1.10
SET invertX 1
SET invertY 0
```

Wait for each reply, including the last three examples. Actions use the same SET path:

```text
SET rightAction key:03:17
SET middleAction disabled
SET leftAction mouse:right
```

Historical pre-C.2 compatibility: replies contained seven fields; older clients ignored additions. That UI accepted old four-field responses without any Action fields, left Pointer usable, and disabled Buttons with “Firmware update required for button mapping.” Partial Action fields or invalid actions were rejected. The current schema requirement above supersedes old four-field compatibility.

#### RESET

Restores all RAM settings, including Pointer/Wheel and three actions, to defaults. No arguments, reboot, or Flash write. Reply has GET's structure with command RESET. Without a subsequent SAVE, reboot reloads prior saved settings. To persist defaults, wait for RESET success then SAVE.

#### SAVE

No arguments. Validate current DeviceConfig, write Flash, then require readback/content/CRC/value validation before success. The returned config is the verified saved value. RAM config and motion fractions are unchanged. Identical stored records skip erase/program and succeed.

```text
SAVE
@CONFIG {"ok":true,"command":"SAVE","config":{"pointerSensitivity":1.0,"middleSensitivity":0.25,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right","wheelSensitivityX":0.25,"wheelSensitivityY":0.25,"pointerInvertX":false,"pointerInvertY":false,"wheelInvertX":false,"wheelInvertY":false}}
```

Distinguish invalid configuration from storage/verification failure:

```text
@CONFIG {"ok":false,"error":"INVALID_CONFIG"}
@CONFIG {"ok":false,"error":"SAVE_FAILED"}
```

Errors leave RAM unchanged; failed storage content is not guaranteed. Without a success reply, the host must not show Saved. After timeout, GET recovers RAM state but cannot prove Flash success. Do not use a late SAVE reply as the resync GET; retry SAVE after resync if needed. Old NOT_IMPLEMENTED replies remain normal device errors.
SAVE is explicit, never automatic after every SET. Configurator flushes drafts before SAVE and disables settings during that operation. Reconnect GET leaves saved status unconfirmed.

### Flash storage format and boot

Pico stores 44-byte v3 at offset zero of the existing final sector of 16 MiB Flash, `0x10FFF000–0x10FFFFFF`. Layout/erase/program/readback/explicit-SAVE policy remains C.1's. Arduino uses the same codec with Philhower 6.1.0 EEPROM emulation. No filesystem; integers are little-endian and floats IEEE-754 binary32, independent of C++ padding/bool representation.

| Offset | Bytes | Contents |
| --- | --- | --- |
| 0 | 4 | ASCII RPNT magic, uint32 0x544E5052 |
| 4 | 2 | Version 3 |
| 6 | 2 | Length 44 |
| 8 / 12 | 4 each | pointerSensitivity / wheelSensitivityX |
| 16 / 17 | 1 each | pointerInvertX / pointerInvertY, 0/1 |
| 18 | 2 | Reserved zero |
| 20 / 24 / 28 | 1 each | Left / Middle / Right action type |
| 21 / 25 / 29 | 1 each | Action code |
| 22 / 26 / 30 | 1 each | Modifiers |
| 23 / 27 / 31 | 1 each | Reserved zero |
| 32 | 4 | wheelSensitivityY |
| 36 / 37 | 1 each | wheelInvertX / wheelInvertY, 0/1 |
| 38 | 2 | Reserved zero |
| 40 | 4 | CRC-32/ISO-HDLC over bytes 0–39 |

CRC reflected polynomial is 0xEDB88320, initial/final XOR 0xFFFFFFFF. Format extensions update version/length and provide migration as needed.
v1 (24 B) and v2 (36 B) remain readable. Old offsets 8/12 are pointerSensitivity/middleSensitivity, 16/17 invertX/Y, 18/19 reserved. CRC offset is 20 for v1 and 32 for v2. v2 actions at 20–31 match v3; v1 adds defaults. Preserve old Pointer values, copy middleSensitivity to both Wheel sensitivities, and initialize both Wheel inversions false, never inheriting Pointer inversion. Validate CRC/values/reserved, migrate only in RAM, write v3 on the next explicit SAVE. Unknown/invalid records fall back to all defaults.

Load once before PS/2 IRQ/HID initialization. Validate magic/version/length/CRC/reserved/bools/finite sensitivities within 0–10/action fields. Erased/missing/invalid storage uses defaults. Boot/fallback/SET/RESET never write or automatically repair Flash.
Arduino EEPROM.begin allocates a 256-byte RAM buffer at boot; SAVE commits then repeats begin with the same size to reload without reallocating. Do not use EEPROM.end because it implicitly commits. Single-sector erase/program is not power-loss atomic: interrupted writes may lose old values. Next boot validates/falls back; no redundancy/wear leveling.
Commit briefly masks IRQs, pausing HID/PS/2 and potentially losing movement/button transitions. Save while stationary and keep power until done. After SAVE, main loop drops pending FIFO/partial frames/packets and waits for a new packet gap. ISR, ordinary decoding, pointer transformation, and debounce remain unchanged.

### Errors

All errors use `@CONFIG {"ok":false,"error":"CODE"}` without echoing the input. Associate the error with the one outstanding request.

| CODE | Meaning |
| --- | --- |
| UNKNOWN_COMMAND | Unknown command |
| INVALID_ARGUMENTS | Missing/extra arguments |
| UNKNOWN_KEY | Unknown SET key |
| INVALID_VALUE | Invalid/out-of-range value |
| INVALID_LINE | Excessive length or invalid characters |
| INVALID_CONFIG | Invalid RAM config for SAVE; no write |
| SAVE_FAILED | Flash write or readback verification failed |
| NOT_IMPLEMENTED | Legacy unsupported SAVE; not returned by current firmware |

### Hardware checks and future work

- Compare default direction, slow/fast motion, L/M/R press/release, drag and historical Middle 0.40× baseline; use current native Wheel expectations on Pico.
- Compare fractions for tiny motion, positive/negative reversals, and Middle transitions.
- Check GET, SET middleSensitivity 0.25, SET pointerSensitivity 1.10, axis inversion and RESET.
- Check sensitivity 0/10, saturation without wrap, and independent buttons.
- Check fragmented sends, CR/LF/CRLF, blanks, invalid values/keys/argument counts, and recovery with GET after overlong lines.
- Check SET→SAVE→power cycle, volatile SET/RESET, RESET→SAVE defaults, and missing/invalid-record fallback.
- Check packet resync and motion/buttons/debug after SAVE.
- Check DEBUG_INPUTS 0/1, JSON amidst debug, HID during Serial disconnect/reconnect, and drop counters during continuous movement.

See [button/migration acceptance](development/validation.md#ボタン割当の実機確認). Possible future work: power-loss redundancy and long-running hardware checks. WebHID would require separate configuration reports/transport. The historical Arduino implementation retained its Mouse report and added the Keyboard library descriptor.

### PING / Device status heartbeat

Request:

```text
PING
```

Response, without config:

```text
@CONFIG {"ok":true,"command":"PING"}
```

No arguments; extras return INVALID_ARGUMENTS. PING is serialized like other requests. Successful SET and syntactically valid GET/RESET/SAVE/PING update activity. Failed storage on a valid SAVE still updates activity and Error. Unknown commands/invalid SET/parser errors show Error without updating activity. Activity remains valid for less than 6000 ms using unsigned subtraction for millis rollover; an open Serial port alone is insufficient.
After GET synchronization, probe PING at the first idle opportunity. Success enables a 2000 ms heartbeat; UNKNOWN_COMMAND stops only that session's timer without fatal UI error, retaining schema-compatible configuration. Reconnect discards capability and probes again. Timeout follows existing GET resync; write failure disconnects.
Skip PING during drafts (including debounce), SET/RESET/SAVE, GET sync, or shortcut recording. Never accumulate heartbeat requests. A sent PING still awaits its reply before prioritizing waiting user work. Disconnect/stream end/unplug stops the timer. A success reply with a different command cannot complete the current request.
LED meaning/priority is unchanged in C.2. Every config error holds RED for two seconds, extended by later errors. Submit PURPLE immediately before SAVE and retain only the LED display for at least 150 ms from start. RED overrides PURPLE; never delay Flash/protocol/HID for display duration. Arduino NeoPixel latch wait yields Serial polling instead of blocking.
