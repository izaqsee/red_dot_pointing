# RedPoint Serial configuration protocol

## 接続とフレーミング

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

## 設定

| キー | default | SETで受け付ける値 | 意味 |
| --- | --- | --- | --- |
| pointerSensitivity | 1.00 | 0～10（両端を含む） | logical Mouse Middleがheldでないときの倍率 |
| middleSensitivity | 0.40 | 0～10（両端を含む） | logical Mouse Middleがheldのときの倍率 |
| invertX | false | `0` / `1` | USB X方向の反転 |
| invertY | false | `0` / `1` | USB Y方向の反転 |
| leftAction | `mouse:left` | 下記Action文字列 | Left物理ボタンの割当 |
| middleAction | `mouse:middle` | 下記Action文字列 | Middle物理ボタンの割当 |
| rightAction | `mouse:right` | 下記Action文字列 | Right物理ボタンの割当 |

感度は数字と小数点1個までの非負10進表記。`1`、`0.35`、`.5`、`1.`は有効。
符号、指数表記、NaN、Infinity、16進表記、単位付き文字列は無効。
floatで保持し、GETでは小数点以下6桁を返すため丸めがある。
boolの応答はJSON booleanだが、SETの入力は`0`/`1`に限定する。
0はそのモードのpointer移動を停止するが、ボタンは動作する。

Middle時はmiddleSensitivityを直接選択し、pointerSensitivityとは乗算しない。
baselineのUSB軸変換（X=PS/2 Y、Y=PS/2 X）と±127 clampの後、
invertX/Yと選択された感度を適用する。小数部はモードごとに蓄積する。
反対のモードでpacketを処理したとき、そのモードを離れた残量を消す。
これはbaselineのMiddle残量の消去タイミングを維持するためである。
出力は再度±127に飽和させる。飽和した整数分は捨て、小数部のみ持ち越す。
SET/RESET成功時は全残量を消し、次のpacketから設定を適用する。
同じ値のSETでも残量は消す。debounceとPS/2受信設定は変更しない。

### Button Action

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

## コマンドと応答

### GET

現在のRAM設定を取得する。引数なし。

```text
GET
@CONFIG {"ok":true,"command":"GET","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.400000,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right"}}
```

### SET <key> <value>

1項目をRAMへ即時適用する。成功時は変更後の全設定を返す。
検証に失敗した場合、設定と移動残量は変更しない。

```text
SET middleSensitivity 0.25
@CONFIG {"ok":true,"command":"SET","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.250000,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right"}}
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

成功応答のconfigには常に7項目を含む。旧Configuratorは追加fieldを無視できる。
新ConfiguratorはAction fieldが3つともない旧4項目応答も許可し、Pointerを利用可能に保つ。
この場合Buttonsは「Firmware update required for button mapping」で無効化し、Action SETを送らない。
一部だけAction fieldがある応答や不正Actionは設定として受理しない。

### RESET

Pointerと3つのButton Actionを含むRAM設定をdefaultに戻す。引数なし。再起動やFlash操作はしない。
成功応答はGETと同じ構造で`command`が`RESET`になる。
Flashの保存内容は変更しない。RESET後にSAVEしなければ、再起動後は以前保存した値へ戻る。
defaultを永続化するにはRESET成功後にSAVEする。

### SAVE

引数なし。現在のRAM上DeviceConfigを検証してFlashへ保存する。
保存後のFlash再読込・内容一致・CRC/値検証まで成功したときだけ、以下を返す。
応答のconfigは保存が確認できた値。RAM設定と移動の小数残量は変更しない。
Flash上のレコードと同一の場合はwrite/eraseを省略して成功を返す。

```text
SAVE
@CONFIG {"ok":true,"command":"SAVE","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.250000,"invertX":false,"invertY":false,"leftAction":"mouse:left","middleAction":"mouse:middle","rightAction":"mouse:right"}}
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

## Flash保存形式と起動

Philhower RP2040 core 6.1.0標準EEPROM emulationを使用する。
coreが予約するFlash末尾の4 KiB sector内、offset 0に次の36-byte v2レコードを保存する。
filesystemは不要。多byte値はlittle endian、floatはIEEE-754 binary32。
C++ structのpaddingやboolのメモリ表現には依存しない。

| Offset | Bytes | 内容 |
| --- | --- | --- |
| 0 | 4 | magic: ASCII `RPNT`（uint32 0x544E5052） |
| 4 | 2 | format version: 2 |
| 6 | 2 | record length: 36 |
| 8 | 4 | pointerSensitivity |
| 12 | 4 | middleSensitivity |
| 16 | 1 | invertX: 0 / 1 |
| 17 | 1 | invertY: 0 / 1 |
| 18 | 2 | reserved: 0 |
| 20 / 24 / 28 | 各1 | Left / Middle / Right action type |
| 21 / 25 / 29 | 各1 | action code |
| 22 / 26 / 30 | 各1 | modifiers |
| 23 / 27 / 31 | 各1 | reserved: 0 |
| 32 | 4 | bytes 0～31のCRC-32/ISO-HDLC |

CRCはreflected polynomial 0xEDB88320、初期値0xFFFFFFFF、最終XOR 0xFFFFFFFF。
拡張時はformat versionとrecord lengthを更新し、必要に応じて移行処理を追加する。
v1 (24 bytes)も読み込む。v1はoffset 0～19が同じ構成で、version=1、length=24、
offset 20～23にbytes 0～19のCRCを持つ。CRC・値・reservedを検証した有効なv1は
既存4項目を保持し、defaultの3 Actionを追加してRAM上だけで移行する。
起動時には書き込まず、次の明示SAVEでv2を保存する。未知versionはdefaultへfallbackする。

setupのPS/2割り込み・HID初期化前に1回loadする。
magic、version、長さ、CRC、reserved、bool表現、感度の有限性・0～10の範囲、Actionのtype/code/modifiersをすべて検証する。
未保存（消去済み領域を含む）またはどれか不正なら7項目すべてdefaultに戻す。
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

## エラー

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

## 実機確認と次phase

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

Button・migrationの実機acceptance手順は[README](../README.md#ボタン割当の実機確認)を参照。
次phase候補は保存中の電源断に対する二重化、実機での長時間検証。
将来WebHIDへ移行する場合は設定用HID reportとtransportを別途設計する。
Mouse reportは既存のまま、KeyboardライブラリのHID descriptorを追加している。

## PING / Device status heartbeat

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

LEDはfirmware側の状態から決定し、protocolのconfig形式・Flash format v2は変更しません。
全`@CONFIG` error応答でREDを2秒保持し、新しいエラーで延長します。
SAVE直前にPURPLEを送信し、完了後もLEDだけ開始時刻から最低150 ms保持します。
ErrorはPURPLEより優先されます。Flash／protocol／HIDを表示時間のために待たせません。
NeoPixelのハードウェアラッチ期間中はSerial pollを次loopへ譲り、blocking waitを回避します。
