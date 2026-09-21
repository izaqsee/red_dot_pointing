# RedPoint

TrackPoint互換モジュールのPS/2入力をRP2040でUSB HID Mouseへ変換する、
自作ポインティングデバイスです。独立したL/M/Rボタンと、Middle押下中の感度変更に対応します。

## Firmwareのbuild / upload

実機確認済みの環境:

- Arduino RP2040 (Philhower) core **6.1.0**
- FQBN: `rp2040:rp2040:vccgnd_yd_rp2040`
- 現在のSerial port: **COM13**（環境によって変更してください）

Arduino CLIをPATHに追加し、上記coreをインストール済みの環境で、repo直下から実行します。
Arduino IDE同梱のCLIを使う場合は、`arduino-cli`をその実行ファイルのパスに置き換えます。

```powershell
arduino-cli compile --fqbn rp2040:rp2040:vccgnd_yd_rp2040 firmware/redpoint
arduino-cli upload --fqbn rp2040:rp2040:vccgnd_yd_rp2040 --port COM13 firmware/redpoint
```

VS Codeでは既存の`.vscode/tasks.json`から次のタスクを実行できます。

- **RedPoint: Build**（既定のbuild task）
- **RedPoint: Build & Upload**（build後、COM13へupload）

Arduino IDEでは`firmware/redpoint/redpoint.ino`を開きます。
同じフォルダの`config.h` / `config.cpp`も必要です。
upload前にConfiguratorのDisconnectを押し、Serial Monitorなども閉じてください。
ファームウェアはArduino標準USBのMouseライブラリを使用します。既存のUSB設定を維持してください。

## Configuratorを起動する

`configurator/`のVanilla HTML / CSS / JavaScriptだけで動作します。
framework、npm install、build、backend処理、外部API、CDNは不要です。
**Web Serial API**で、ブラウザからUSB Serialポートへ直接接続します。

repo直下で、Pythonによるローカルの静的ファイル配信を起動します。

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory configurator
```

デスクトップ版ChromeまたはEdgeで[http://localhost:8000](http://localhost:8000)を開きます。
Web Serialはsecure context（localhostまたはHTTPS）で使用します。
ブラウザの対応状況とsecure contextを画面で検出し、利用できない場合は理由を表示します。
このHTTPサーバは静的ファイルの配信だけを行い、デバイスの設定値やSerial dataは受け取りません。
終了はターミナルでCtrl+Cです。

1. USBでRedPointを接続し、Serial Monitorなど他のポート利用アプリを閉じる。
2. **Connect**を押し、ブラウザのポート選択画面でRedPointを選ぶ。
3. GETの応答後、**Connected**となり、設定controlが有効になる。
4. sliderで感度、checkboxで反転を変更する。操作値と「デバイス確認値」は別表示。
5. **Reset to defaults**はデバイスへRESETを送り、その応答で表示を同期する。
6. 終了時は**Disconnect**を押す。

感度の範囲は0～10、sliderの刻みは0.01です。0はそのモードのpointer移動を停止します。
Middle sensitivityは通常感度と乗算せず、Middle押下中に直接選択される倍率です。
defaultは通常1.00、Middle 0.40、反転なしです。

**現在の設定はRAMのみです。デバイスを再起動・電源断するとdefaultへ戻ります。**
Configuratorの切断だけでは設定をRESETしませんが、接続操作に伴うデバイスの再起動があればdefaultへ戻ります。
永続的なSAVEは未実装のため、Saveボタンは無効表示で、SAVEコマンドは送信しません。

設定値・Serial dataの外部送信、analytics、localStorageへの保存は行いません。
すべて同梱のローカルassetを使い、CSPでもアプリのネットワーク接続を無効化しています。
将来は同じ静的ファイルをGitHub Pagesでhostする予定ですが、今回はdeployしていません。

## 通信とエラー時の動作

通信仕様のsource of truthは[docs/protocol.md](docs/protocol.md)です。
firmware側のprotocolはConfigurator追加時に変更していません。

- 115200 baud / 8N1 / flow controlなし。`@CONFIG`と`@DEBUG`を分類する。
- 分割された受信chunkを行へ復元する。debug・未知行・不正JSONは設定へ混ぜず無視する。
- `@CONFIG`の型・範囲を検証し、現在の要求に対応する応答だけを確定値にする。
- 120msのdebounceと項目ごとの最新値への集約を行い、要求は必ず1つずつ送る。
- RESETは未送信変更を破棄し、送信中SETの応答を待ってから実行する。
- 2秒のtimeout時は、適用済みか不明なので未送信変更を破棄し、改行＋GETで再同期する。
  遅れて届くSET/RESET応答やエラーは再同期GETの応答に使わない。
  再同期にも失敗した場合は切断し、再接続を案内する。
- 切断時は保留要求を中止し、stream lockを解放してポートを閉じる。
  USB抜去・read/writeエラー後もConnectからやり直せる。

`app.js`内のprotocol処理、Serial transport、UIは関数単位で分離しています。
WebHID移行時はtransportを差し替える設計ですが、HID reportの設計・firmware対応は今後必要です。
Web SerialのAPI利用は[Chrome公式資料](https://developer.chrome.com/docs/capabilities/serial)を参照しています。

## 検証

Node.js 20以降があれば、外部依存なしで通信・状態遷移のテストを実行できます。
Nodeはテスト専用で、Configuratorの利用には不要です。

```powershell
node --test tests/configurator.test.cjs
```

テストはmockのSerial streamを使用します。実機では以下を確認してください。

1. 接続直後のGETで、ブラウザを開く前のデバイス設定が表示される。
2. Normal / Middleの感度変更とInvert X/Yが実際の移動に反映される。
3. sliderを連続操作しても、最終値がデバイス確認値と一致する。
   応答待ちに別項目や同じ項目を変更しても、最新操作値が消えない。
4. Middle押下、L/M/Rボタン、dragなどの既存HID動作が継続する。
5. pointer / button debugが大量に混在しても設定取得・変更できる。
6. RESET後にdefaultのデバイス確認値になり、実機の感度・反転も戻る。
7. Disconnect後のcontrol無効化、再接続、USB抜去中・送信中の切断から復帰できる。
8. ポート選択キャンセル、他アプリがCOM13を使用中、非対応ブラウザの表示を確認する。
9. 電源再投入後はdefaultになる。Saveは無効で永続化を行わない。

## 次phase候補

- 実機での長時間操作・debug負荷を含む検証とUX調整
- Flash保存方式、設定version、書込頻度の設計とpersistent SAVE
- GitHub Pagesへの静的hosting
- 必要になった段階でのWebHID用transport / firmware report設計

firmware update、button remapping、acceleration curve editorは未実装です。
