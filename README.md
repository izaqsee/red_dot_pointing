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
同じフォルダの`config*`ファイル（設定・レコード符号化・保存処理）も必要です。
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

Web Serial対応のChromium系ブラウザ（デスクトップ版ChromeまたはEdge）で[http://localhost:8000](http://localhost:8000)を開きます。
Web Serialはsecure context（localhostまたはHTTPS）で使用します。
ブラウザの対応状況とsecure contextを画面で検出し、利用できない場合は理由を表示します。
このHTTPサーバは静的ファイルの配信だけを行い、デバイスの設定値やSerial dataは受け取りません。
終了はターミナルでCtrl+Cです。

1. USBでRedPointを接続し、Serial Monitorなど他のポート利用アプリを閉じる。
2. **Connect**を押し、ブラウザのポート選択画面でRedPointを選ぶ。このユーザー操作でUSB deviceへのpermissionを付与する。
3. GETの応答後、**Connected**となり、設定controlが有効になる。
4. sliderで感度、checkboxで反転を変更する。操作値と「デバイス確認値」は別表示。
5. 永続化するには**Save**を押す。未送信SETの反映後、SAVEの成功応答で**Saved**になる。
6. **Reset to defaults**はRAMのみdefaultへ戻す。defaultも保存する場合は続けて**Save**を押す。
7. 終了時は**Disconnect**を押す。

感度の範囲は0～10、sliderの刻みは0.01です。0はそのモードのpointer移動を停止します。
Middle sensitivityは通常感度と乗算せず、Middle押下中に直接選択される倍率です。
defaultは通常1.00、Middle 0.40、反転なしです。

## Runtime設定とFlash保存

- **SET**: 実機の動作へ即時反映するRAM設定。Flashを書き換えない。
- **SAVE**: 現在の設定をFlashへ明示保存し、再起動後も読み込む。
- **RESET**: RAMのみdefaultに戻す。SAVEしなければ再起動後は以前の保存値に戻る。
- **RESET → SAVE**: defaultをFlashへ保存する。
- **未保存・不正な保存データ**: 起動時に4項目すべてdefaultへfallbackする。

保存にはPhilhower core 6.1.0のEEPROM emulationを使用します。filesystemは使いません。
24-byteの固定形式にmagic、version、record長、設定、CRC32を保持し、
起動時に整合性・値の範囲を検証します。詳細は[通信仕様](docs/protocol.md#flash保存形式と起動)を参照してください。
SET、RESET、起動時はFlashを書かず、SAVE時も前回保存内容と同一ならerase/writeを省略します。

Save成功はデバイスでのcommitとFlash再読込照合が完了した後に通知されます。
UIは応答で確認した値を**Saved**とし、設定を変えると**Unsaved changes**にします。
同じ接続中に保存確認済みの値へ戻した場合もSavedになります。
再接続時のGETはRAMだけを取得するので**保存状態未確認**と表示し、Flash保存済みとは推測しません。
設定値をlocalStorageへ保存しません。SAVEの失敗・timeoutはSaved扱いにせず再試行できます。
旧firmwareではSAVEがNOT_IMPLEMENTEDになります。永続化には更新後のfirmwareが必要です。

保存中はFlash操作のため割り込みとHID/PS/2処理が短時間停止します。
デバイスを静止させてSaveし、完了までUSBを抜かないでください。
書込み後は途中frame/FIFOを破棄してpacket gapで同期を取り直します。
単一sectorなので、保存途中の電源断で以前の保存内容が失われる可能性があります。
不正なデータは次回起動時にdefaultへfallbackします。二重化・wear levelingは未実装です。
EEPROM領域やFlash容量の変更、全消去を伴う書き込みでは保存値が失われる場合があります。

設定値・Serial dataの外部送信、analytics、localStorageへの保存は行いません。
すべて同梱のローカルassetを使い、CSPでもアプリのネットワーク接続を無効化しています。
GitHub Pages版でもデバイスデータは外部serverへ送りません。GitHubには静的ページの通常の取得だけが発生します。
今回追加したworkflowは未deployで、GitHub側の設定変更も行っていません。

## 通信とエラー時の動作

通信仕様のsource of truthは[docs/protocol.md](docs/protocol.md)です。
GET / SET / RESETの形式は維持し、SAVEの成功・失敗応答だけを追加しています。

- 115200 baud / 8N1 / flow controlなし。`@CONFIG`と`@DEBUG`を分類する。
- 分割された受信chunkを行へ復元する。debug・未知行・不正JSONは設定へ混ぜず無視する。
- `@CONFIG`の型・範囲を検証し、現在の要求に対応する応答だけを確定値にする。
- 120msのdebounceと項目ごとの最新値への集約を行い、要求は必ず1つずつ送る。
- RESETは未送信変更を破棄し、送信中SETの応答を待ってから実行する。
- SAVEは未送信SETをすべて反映してから送る。SET失敗時は保留SAVEも中止し、保存を誤認させない。
- 2秒のtimeout時は、適用済みか不明なので未送信変更を破棄し、改行＋GETで再同期する。
  遅れて届くSET/RESET/SAVE応答やエラーは再同期GETの応答に使わない。
  GETだけでは保存成功を判定できないため保存状態は未確認とし、必要ならSaveを再実行する。
  再同期にも失敗した場合は切断し、再接続を案内する。
- 切断時は保留要求を中止し、stream lockを解放してポートを閉じる。
  USB抜去・read/writeエラー後もConnectからやり直せる。

`app.js`内のprotocol処理、Serial transport、UIは関数単位で分離しています。
WebHID移行時はtransportを差し替える設計ですが、HID reportの設計・firmware対応は今後必要です。
Web SerialのAPI利用は[Chrome公式資料](https://developer.chrome.com/docs/capabilities/serial)を参照しています。

## GitHub Pages deployment

[.github/workflows/pages.yml](.github/workflows/pages.yml)はGitHub公式の
configure-pages / upload-pages-artifact / deploy-pagesを使い、**configurator/だけ**を公開します。
CSS / JSはrelative pathなので、`https://<user>.github.io/<repository>/`のproject Pagesにも対応します。
PagesのHTTPSはWeb Serialのsecure context要件を満たします。backendやbuild toolは不要です。

人間がGitHub側で行う操作:

1. GitHub repositoryを用意し、任意のremoteを設定して変更をpushする（この作業ではpushしていません）。
2. repositoryの**Settings → Pages → Build and deployment → Source**で**GitHub Actions**を選ぶ。
3. **Settings → Actions → General**でActionsと使用するGitHub公式Actionsが許可されていることを確認する。
   組織のpolicyでPages / OIDCが制限されている場合は管理者に確認する。
4. workflowをdefault branchへ配置する。default branchはrepository情報から判定するため、main固定ではない。
5. **Actions → Deploy Configurator to GitHub Pages → Run workflow**でdefault branchを選び初回deployする。
   以後はdefault branchの`configurator/**`、Webテスト、Pages workflowの変更で自動deployする。
   他branchでの実行はjobをskipする。
6. `github-pages` environmentに承認ルールがある場合は承認する。branch制限を使う場合はdefault branchを許可する。
7. Actionsのdeployment URLまたはSettings → Pagesの公開URLを開く。

workflowはWebテストに成功してからartifactをuploadし、deployします。
`contents: read`、`pages: write`、`id-token: write`を使用します。
環境のPages設定・repository設定は自動変更しません。README / firmware / testsは公開artifactに含めません。
GitHubプランやrepository可視性に応じてPagesを利用可能なrepositoryを用意してください。
手順の根拠は[GitHub公式Pages workflow資料](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)です。

## 検証

Node.js 20以降があれば、外部依存なしで通信・状態遷移のテストを実行できます。
Nodeはテスト専用で、Configuratorの利用には不要です。

```powershell
node --test tests/configurator.test.cjs
python tests/run_firmware_tests.py
```

firmware hostテストにはC++ compilerが必要です。WindowsではVisual Studio C++ Build Toolsを自動検出し、
Linux/macOSではc++ / g++ / clang++を使います。CXXでcompiler実行ファイルも指定できます。
本番の設定・保存コードをmock EEPROMで実行し、実機へアクセスしません。
record破損、範囲外、CRC、default fallback、SET/RESETの非永続性、SAVEの検証・失敗・再読込・同値書込省略を確認します。

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
9. SET→SAVE成功→電源再投入で保存値へ戻る。SETのみ→再起動では最後の保存値へ戻る。
10. RESETのみ→再起動で以前の保存値へ戻り、RESET→SAVE→再起動ならdefaultになる。
11. SAVE失敗・timeoutでSaved扱いにならず再試行できる。保存後の移動・Middle・3ボタンとdebugの復帰を確認する。
12. 未保存領域、不正magic/version/CRC/値のfixtureでdefault fallbackを確認する（通常利用機のFlashを不用意に壊さず、テスト機で行う）。
13. Pagesのproject URLからCSS/JSが読み込まれ、Connect・SET・SAVE・再接続できる。

## 次phase候補

- 実機での長時間操作・debug負荷を含む検証とUX調整
- 保存途中の電源断対策（二重化）、format migration、書込寿命の評価
- GitHub Pages初回deploymentと実機からの接続確認
- 必要になった段階でのWebHID用transport / firmware report設計

firmware update、button remapping、acceleration curve editorは未実装です。
