# Pointer / Wheel transfer function

基準: remote main `6f83848b0bd3bdb878b55d28ce602a56b5175379`。
対象はPico SDK runtime。旧Arduino sketch、USB/network、PS/2 decoder、ボタン・LEDの意味は変更しない。

## 入力からHIDまで

1. 現行どおりPS/2 signed byteを読み、X/Y交換と[-127,127] clamp。raw -128も従来どおり-127。
2. 固定変換後の座標で方向補正。X<0 Left、X>0 Right、Y<0 Up、Y>0 Down。
   これは既存Pointer出力とnative testsの座標（右＋、下＋）に対応する。user invertより先。
3. Pointerは既存pointerSensitivityのみ。Pointer curve/accelerationなし。
4. Wheelは補正後の入力から `t=min(1,max(abs(x),abs(y))/127)` を計算。
   `g=low+(high-low)*t*t*(3-2*t)` を両軸共通で適用。
5. Wheel出力候補は `corrected axis * g * wheelSensitivityX/Y * WHEEL_BASE_SCALE`。
   `WHEEL_BASE_SCALE=0.1f` はHID tickへの固定変換。Pointer baseは従来の1。
6. user invertとWheel垂直基準反転は従来の符号規則。PanはX、WheelはYを出力。
7. fractional accumulator、整数切捨て、[-127,127]飽和と過剰整数分破棄、
   mode変更・SET/RESETでの端数クリアは従来どおり。

magnitudeは速度（counts/second）ではなくパケット当たりの強度。パケットrate補正は行わない。
補正gainが1を超える方向ではmagnitude=127のplateauへ早く到達する。
Low=Highなら定数gain。Low<Highは単調増加、Low>Highは単調減少のgainを許容する。
後者は減速用であり、最終出力量の単調増加までは保証しない。
全新規gain=1で既存mainと同じ計算結果になる。
最大積は127*10*10*10*0.1=12700なので既存int16変換範囲内。

## Config / protocol / Flash

追加floatは以下の6個。すべて有限値0〜10、初期値1.0。0はその補正／response側の出力をゼロにできる。

- `directionalGainLeft`, `directionalGainRight`, `directionalGainUp`, `directionalGainDown`
- `wheelLowGain`, `wheelHighGain`

GET/SET/RESET/SAVEの通常configとして扱い、変更検出・未保存判定・検証・CRCに含める。
旧 `middleSensitivity` / `invertX` / `invertY` aliasも維持。
JSONは従来どおり小数6桁。応答領域を512から768 bytesへ拡張し、最大値とshortcutを含む応答を
実際のlwIP/CDCで比較済み。HTTP wire buffer、transport、USB descriptorsは変更していない。

v3は固定長44 bytes＋version/length/CRC検証なので、予約領域だけで24 bytes追加することはできない。
v4は68 bytes。offset 0〜39を保持し、新規gainを40,44,48,52,56,60に格納、CRCは64。
v1(24 bytes)、v2(36 bytes)、v3(44 bytes)はそれぞれ元のCRC位置で検証し、
既存設定を保持して新規gain=1へRAM上で移行する。起動・GETだけではFlashを書かず、SAVEでv4保存。
旧firmwareへのdowngrade時はv4を読めないため、旧設定への自動逆移行はしない。

## Configurator

既存3カード、toolbar、ショートカット操作、Wheelの対数sliderを維持。
Pointerカードに共通CalibrationのLeft/Right/Up/Down、WheelカードにSlow/Fastを追加。
新gainは0.01刻み。SVGでgain対magnitudeのsmoothstepを表示（自由曲線編集なし）。
編集途中の値もpreviewへ反映し、確認済み値・Save・Resetは既存の応答確認フローを使う。
旧firmwareの応答に新規fieldがない場合、既存操作を維持し、新gainのみ無効化・update案内を表示。
依存ライブラリは追加していない。

## 検証

```powershell
python tests/run_firmware_tests.py
python firmware/redpoint_pico/tests/run_integration.py
$env:REDPOINT_PICO_GET_RESPONSE = (Resolve-Path firmware/redpoint_pico/build/host-get-response.txt).Path
node --test tests/configurator.test.cjs
python tests/http_fsdata_test.py
python firmware/redpoint_pico/tests/check_transfer_scope.py
cmake --build firmware/redpoint_pico/build --parallel 8
```

- 共通config host tests成功。独立v1/v2/v3 fixture、v4 roundtrip、CRC、不正gain、GET/SET/RESET/SAVEを確認。
- Pico/lwIP native integration成功。実際のPS/2入力経路でmain計算式との20,480パケット比較、
  4方向だけへの補正、invert順序、端数、極性、saturation、mode切替を検証。
- gain曲線は0.01 count刻みで増加／減少／定数・連続性・上限clamp・両軸共通を検証。
- 旧C.1/C.2 scroll回帰はresponse=10でbase0.1を明示相殺し、従来の単位scaleの期待値を維持。
  identity設定の現行main比較は別途上記20,480パケットで行う。
- Configurator 66 tests成功（実Pico backendのGET capture使用、skipなし）。C.2/C.3 UI回帰を含む。
- ローカルのメモリ内mockだけに接続し、ブラウザーで配置とFast変更時のpreview更新を確認。
- ARM MinSizeRelビルド成功。ELF/UF2、descriptor、外部TinyUSB由来、埋込assetの一致を確認。
- `check_transfer_scope.py` は基準mainから許可対象以外のfirmware、PS/2関数、ボタン処理、
  mode切替、frontend transportが不変であることを確認。
- 旧 `check_milestone_c2.py` / `check_milestone_c3.py` のhistorical hash gateは現行mainでも
  `windows_ncm.h`等が旧fixtureと不一致。Windows checkoutの改行にも依存する。
  またC.3はconfig全体の不変を要求するため今回のschema拡張とは両立しない。
  旧fixture・旧検査を更新して合格扱いにはせず、現在のscope検査と動作回帰を併用する。

## 実機評価で残る確認

- 現物の取り付け方向でLeft/Right/Up/Down表示が指の物理方向と合うか。
- Pointerで物理補正を調整したあと、同じ補正がWheel/Panでも自然か。
- Slowを下げFastを上げた際の微小移動・斜め移動・連続スクロールの操作感。
- Windows/iPad各アプリのWheel/Pan方向、OS側スクロール設定との組合せ。
- v1/v2/v3設定の実機起動、SAVE、電源再投入、旧UIとの接続。
- USB複合機能・中ボタンhold・LEDが従来どおり動くか。

実機uploadおよびremote pushは実施していない。
