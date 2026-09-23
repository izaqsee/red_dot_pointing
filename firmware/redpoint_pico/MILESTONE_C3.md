# Milestone C.3 — Compact single-screen Configurator UI

C.2はユーザー実機確認済み。今回はConfiguratorのpresentationだけを変更。
remote push・hardware uploadは行っていない。

## Layout

- 上部toolbar: 左にconnection status / Connect / Disconnect、右に保存状態 / Reset / Save。
- MainはPointer / Wheel / Buttonsの3カードだけ。
- width >= 900px: PointerとWheelを等幅2列、Buttonsは全幅。
- width <= 899px: 1列で通常のdocument scrollを許可。
- width <= 480px: paddingとlabel幅を調整し、toolbarのstatusを折り返す。
- card padding 14px（狭幅12px）、gap 12px、control row gap 8px。
- button / select / slider / checkbox labelの操作領域は高さ44px以上。
- fixed positioning、JS layout計算、overflow hiddenによる強制一画面化は使わない。

削除: 大きなbrand/hero/title/intro、常設の説明文、range目盛り、独立Connection/Actionsカード、footer。
RedPoint名称のh1と既存の確認値/port-infoはscreen reader向けに保持。
PointerとWheelはlabel / slider / valueの1行。Invertも横並び。
ButtonsはLeft/Middle/Rightの3行。Keyboard Shortcut選択行だけkey表示とRecordを展開する。
色、font family、card radius、form/buttonの基本表現、status色を維持。

## app.js変更

UI-onlyなshortcutChoices Setで、キー未記録のKeyboard Shortcut選択を表す。
この選択だけではSETしない。Recordは従来のrecorderを起動し、完了時は従来のaction SET経路へ渡す。
Mouse/Disabledへ戻すと対応行を収納。RESET/disconnect等のdraft破棄時はUI選択もclear。
recorderのkeyboard capture、blur/cancel、validation、protocol、queueは変更していない。
通常のGET/SET完了の説明messageは非表示にし、toolbar/controlで状態を示す。
error、再同期、保存成功など必要なmessageはtoolbar直下へ表示する。文言・通信処理は維持。

## Changed files

- configurator/index.html
- configurator/style.css
- configurator/app.js（mount内のUI表示・選択処理のみ）
- tests/configurator.test.cjs（既存testを維持し、2 test追加）
- firmware/redpoint_pico/milestone_a_freeze.json（今回許可されたCSS hashのみ更新）
- firmware/redpoint_pico/milestone_c2_freeze.json（C.2 source baseline、新規）
- firmware/redpoint_pico/tests/check_milestone_c3.py（新規）
- firmware/redpoint_pico/MILESTONE_C3.md（本報告）

firmware source/build definitionは変更なし。既存A/B/C/C.1/C.2 firmware test本体も変更なし。

## Automated validation

| Check | Result |
| --- | --- |
| Configurator Node tests | 60/60 PASS、skip 0（既存58＋追加2） |
| Firmware host tests | 全PASS |
| Pico / real lwIP integration | 既存11群PASS |
| A/B/C/C.1/C.2 regression | 全PASS |
| C.3 freeze | C.2 firmware source/build definitions・validConfig不変 |
| Protocol / transport / heartbeat / decoder / HID / Flash / LED freeze | PASS |
| fsdata current-assets equality | PASS |
| HTTP/static compile | PASS |
| Final ELF/UF2 build | PASS |

UI testは各fieldのSET、RESET/SAVE、HTTP-first、Serial fallback、recorder captureの既存検査に加え、
3カード/toolbar要素、Keyboard選択時だけのRecord表示、非Keyboard時の収納、未記録選択でSETしないことを確認。
compile-onlyの既存-Wfloat-equal警告3件は継続。通常target buildは警告なし。

## Browser verification

repo内の一時local HTTP previewを127.0.0.1だけで起動し、Codex in-app Chromiumで確認。
接続済みUIはhost testのGET fixtureを返すpreview handlerによるもので、実機へは接続していない。
ブラウザ表示倍率の影響を補正して、以下はDOMのinnerWidth/innerHeightで確認したCSS pixel値。

| CSS viewport | Columns | document scrollHeight | document scrollWidth | Result |
| --- | --- | --- | --- | --- |
| 1280 × 720 | 2 + full-width Buttons | 720 | 1280 | 縦横overflowなし |
| 1024 × 650 | 2 + full-width Buttons | 650 | 1024 | 縦横overflowなし |
| 1024 × 768 | 2 + full-width Buttons | 768 | 1024 | 縦横overflowなし |
| 768 × 1024 | 1 | 1024 | 768 | 縦積み、overflowなし（必要時scroll可） |

toolbar実測は約55.81 CSS px。スクリーンショットでdesktop/portraitのcontrol配置も確認。
実ブラウザでLeftだけKeyboard Shortcutを選ぶとその行のRecordが現れ、Recordで既存capture panelが開き、
CancelしてMouseへ戻すとRecordが隠れることを確認した。
検証後、viewport overrideは解除し、local preview serverは停止。

## Static assets / build

通常CMake buildがtools/generate_http_fsdata.pyを実行し、
build/redpoint-http/fsdata_redpoint.cを更新。最終ELF内の5 routesのassetsがcurrent sourceとbyte一致。
HTTP APIやfirmware sourceは変更していない。

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' --build firmware/redpoint_pico/build --parallel 8
& 'C:/Program Files/Inkscape/bin/python.exe' firmware/redpoint_pico/tests/check_milestone_c3.py
```

Flash 138,236 B、RAM 41,840 B + stack 4,096 B。UF2 276,480 B。
`E:/projects/red_dot_pinting/firmware/redpoint_pico/build/redpoint_reva.uf2`

## Remaining verification

iPad Safari/Chrome実機での表示・touch・browser chrome/zoom差は未検証。
今回のUF2でdevice-hosted pageを配信する実機確認も未実施。
ユーザーによるupload後、toolbarの接続/保存、各感度/Invert、Shortcut記録、portrait回転を確認すること。
