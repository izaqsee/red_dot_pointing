# RedPoint Serial configuration protocol

## 接続とフレーミング

USB CDC Serialを使用する。接続設定は115200 baud、8N1、flow controlなし。
HID MouseとWebSerial設定通信は同時に動作する。明示的なSAVEで設定をFlashへ保存する。
Arduino Philhower RP2040 coreとMouseライブラリを使い、`firmware/redpoint/`
をスケッチとして開く。同じフォルダの`config*.cpp`もビルド対象になる。
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
| pointerSensitivity | 1.00 | 0～10（両端を含む） | Middleを押していないときの倍率 |
| middleSensitivity | 0.40 | 0～10（両端を含む） | debounce後のMiddle押下中の倍率 |
| invertX | false | `0` / `1` | USB X方向の反転 |
| invertY | false | `0` / `1` | USB Y方向の反転 |

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
同じ値のSETでも残量は消す。ボタンのHID送信、debounce、PS/2受信設定は変更しない。

## コマンドと応答

### GET

現在のRAM設定を取得する。引数なし。

```text
GET
@CONFIG {"ok":true,"command":"GET","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.400000,"invertX":false,"invertY":false}}
```

### SET <key> <value>

1項目をRAMへ即時適用する。成功時は変更後の全設定を返す。
検証に失敗した場合、設定と移動残量は変更しない。

```text
SET middleSensitivity 0.25
@CONFIG {"ok":true,"command":"SET","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.250000,"invertX":false,"invertY":false}}
SET pointerSensitivity 1.10
SET invertX 1
SET invertY 0
```

最後の3例もそれぞれ送信後に応答を待つ。

### RESET

RAM設定をdefaultに戻す。引数なし。再起動やFlash操作はしない。
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
@CONFIG {"ok":true,"command":"SAVE","config":{"pointerSensitivity":1.000000,"middleSensitivity":0.250000,"invertX":false,"invertY":false}}
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
coreが予約するFlash末尾の4 KiB sector内、offset 0に次の24-byteレコードを保存する。
filesystemは不要。多byte値はlittle endian、floatはIEEE-754 binary32。
C++ structのpaddingやboolのメモリ表現には依存しない。

| Offset | Bytes | 内容 |
| --- | --- | --- |
| 0 | 4 | magic: ASCII `RPNT`（uint32 0x544E5052） |
| 4 | 2 | format version: 1 |
| 6 | 2 | record length: 24 |
| 8 | 4 | pointerSensitivity |
| 12 | 4 | middleSensitivity |
| 16 | 1 | invertX: 0 / 1 |
| 17 | 1 | invertY: 0 / 1 |
| 18 | 2 | reserved: 0 |
| 20 | 4 | bytes 0～19のCRC-32/ISO-HDLC |

CRCはreflected polynomial 0xEDB88320、初期値0xFFFFFFFF、最終XOR 0xFFFFFFFF。
拡張時はformat versionとrecord lengthを更新し、必要に応じて移行処理を追加する。
現versionでは他versionを移行せずdefaultへfallbackする。

setupのPS/2割り込み・HID初期化前に1回loadする。
magic、version、長さ、CRC、reserved、bool表現、感度の有限性・0～10の範囲をすべて検証する。
未保存（消去済み領域を含む）またはどれか不正なら4項目すべてdefaultに戻す。
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

次phase候補は保存中の電源断に対する二重化、format migration、実機での長時間検証。
将来WebHIDへ移行する場合は設定用HID reportとtransportを別途設計する。
USB descriptorやMouse reportは変更していない。
