# Milestone C.3 — Compact single-screen Configurator UI

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

> 現在の確認状況（2026-09-24）: C.2までの機能とC.3のiPad表示・touch操作は実機確認済みです。以下の未検証記述・サイズ・テスト結果は各Milestone実装時点の履歴です。

C.2は実機確認済み。C.3ではConfiguratorのpresentationだけを変更。

### Layout

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

### app.js変更

UI-onlyなshortcutChoices Setで、キー未記録のKeyboard Shortcut選択を表す。
この選択だけではSETしない。Recordは従来のrecorderを起動し、完了時は従来のaction SET経路へ渡す。
Mouse/Disabledへ戻すと対応行を収納。RESET/disconnect等のdraft破棄時はUI選択もclear。
recorderのkeyboard capture、blur/cancel、validation、protocol、queueは変更していない。
通常のGET/SET完了の説明messageは非表示にし、toolbar/controlで状態を示す。
error、再同期、保存成功など必要なmessageはtoolbar直下へ表示する。文言・通信処理は維持。

### Changed files

- configurator/index.html
- configurator/style.css
- configurator/app.js（mount内のUI表示・選択処理のみ）
- tests/configurator.test.cjs（既存testを維持し、2 test追加）
- firmware/redpoint_pico/milestone_a_freeze.json（C.3のCSS変更に伴うhashのみ更新）
- firmware/redpoint_pico/milestone_c2_freeze.json（C.2 source baseline、新規）
- firmware/redpoint_pico/tests/check_milestone_c3.py（新規）
- firmware/redpoint_pico/MILESTONE_C3.md（本報告）

firmware source/build definitionは変更なし。既存A/B/C/C.1/C.2 firmware test本体も変更なし。

### Automated validation

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

### Browser verification

repo内の一時local HTTP previewを127.0.0.1だけで起動し、local Chromium previewで確認。
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

### Static assets / build

通常CMake buildがtools/generate_http_fsdata.pyを実行し、
build/redpoint-http/fsdata_redpoint.cを更新。最終ELF内の5 routesのassetsがcurrent sourceとbyte一致。
HTTP APIやfirmware sourceは変更していない。

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_c3.py
```

Flash 138,236 B、RAM 41,840 B + stack 4,096 B。UF2 276,480 B。
`firmware/redpoint_pico/build/redpoint_reva.uf2`

### 実装時点で残っていた検証（履歴）

実装時点では、iPad Safari/Chrome実機での表示・touch・browser chrome/zoom差は未検証でした。現在はiPad実機での表示・touch確認が完了しています。
実装時点では、C.3のUF2でdevice-hosted pageを配信する実機確認も未実施でした。
UF2書き込み後、toolbarの接続/保存、各感度/Invert、Shortcut記録、portrait回転を確認すること。

## EN

> Current verification status (2026-09-24): Functionality through C.2 and C.3 iPad display/touch operation have been verified on hardware. Unverified items, sizes and test results below describe the original implementation milestone.

C.2 was verified on hardware. This milestone changed only Configurator presentation.

### Layout

- Top toolbar: connection status / Connect / Disconnect on the left; save status / Reset / Save on the right.
- Main contains only Pointer / Wheel / Buttons cards.
- Width >= 900px: equal-width Pointer/Wheel columns, full-width Buttons.
- Width <= 899px: one column with normal document scrolling.
- Width <= 480px: adjusted padding/label width; toolbar status wraps.
- Card padding 14px (12px narrow), gap 12px, control-row gap 8px.
- Buttons, selects, sliders and checkbox labels have touch areas at least 44px high.
- No fixed positioning, JavaScript layout calculations or overflow-hidden forced single-screen layout.

Removed large branding/hero/title/intro, permanent explanatory text, range ticks, separate Connection/Actions cards and footer. RedPoint h1, confirmed values and port-info remain accessible to screen readers. Pointer/Wheel use label / slider / value rows with inline Invert controls. Buttons has Left/Middle/Right rows; only Keyboard Shortcut rows expand key display and Record. Colors, font family, card radius, basic form/button styling and status colors remain.

### app.js changes

A UI-only shortcutChoices Set represents Keyboard Shortcut selection before a key is recorded. Selection alone sends no SET. Record opens the existing recorder and completion uses the existing action SET path. Returning to Mouse/Disabled collapses the row. RESET/disconnect and other draft-discard operations clear UI selections. Keyboard capture, blur/cancel, validation, protocol and queue are unchanged. Routine GET/SET completion messages are hidden; toolbar/controls show state. Errors, resync and successful-save messages appear below the toolbar, with existing wording and communication behavior.

### Changed files

- configurator/index.html
- configurator/style.css
- configurator/app.js (UI presentation/selection inside mount only)
- tests/configurator.test.cjs (existing tests retained; two added)
- firmware/redpoint_pico/milestone_a_freeze.json (only the hash for the C.3 CSS change updated)
- firmware/redpoint_pico/milestone_c2_freeze.json (new C.2 source baseline)
- firmware/redpoint_pico/tests/check_milestone_c3.py (new)
- firmware/redpoint_pico/MILESTONE_C3.md (this report)

Firmware sources/build definitions and existing A/B/C/C.1/C.2 firmware test bodies are unchanged.

### Automated validation

| Check | Result |
| --- | --- |
| Configurator Node tests | 60/60 PASS, 0 skipped (58 existing + 2 new) |
| Firmware host tests | All PASS |
| Pico / real lwIP integration | Existing 11 groups PASS |
| A/B/C/C.1/C.2 regression | All PASS |
| C.3 freeze | C.2 firmware sources/build definitions and validConfig unchanged |
| Protocol / transport / heartbeat / decoder / HID / Flash / LED freeze | PASS |
| fsdata current-assets equality | PASS |
| HTTP/static compile | PASS |
| Final ELF/UF2 build | PASS |

Existing UI checks cover each field's SET, RESET/SAVE, HTTP-first, Serial fallback and recorder capture. Added checks cover the three cards/toolbar, Record visible only for Keyboard, collapse for other choices and no SET for unrecorded selection. Three existing -Wfloat-equal warnings remain in compile-only checks; normal target builds are warning-free.

### Browser verification

A temporary repo-local HTTP preview bound only to 127.0.0.1 was checked in local Chromium preview. Connected UI used the host-test GET fixture, not a hardware connection. The following CSS pixel dimensions were verified using DOM innerWidth/innerHeight, compensating for browser zoom.

| CSS viewport | Columns | document scrollHeight | document scrollWidth | Result |
| --- | --- | --- | --- | --- |
| 1280 × 720 | 2 + full-width Buttons | 720 | 1280 | No vertical/horizontal overflow |
| 1024 × 650 | 2 + full-width Buttons | 650 | 1024 | No vertical/horizontal overflow |
| 1024 × 768 | 2 + full-width Buttons | 768 | 1024 | No vertical/horizontal overflow |
| 768 × 1024 | 1 | 1024 | 768 | Stacked, no overflow; scrolling available if needed |

Toolbar height measured about 55.81 CSS px. Screenshots confirmed desktop/portrait control placement. Selecting Keyboard Shortcut for Left alone showed Record on that row; Record opened the existing capture panel; Cancel then Mouse hid Record. Viewport overrides were removed and the local preview server stopped afterward.

### Static assets / build

The normal CMake build runs tools/generate_http_fsdata.py and updates build/redpoint-http/fsdata_redpoint.c. Assets for all five routes in the final ELF match current source bytes. HTTP API and firmware sources are unchanged.

```powershell
cmake --build firmware/redpoint_pico/build --parallel 8
python firmware/redpoint_pico/tests/check_milestone_c3.py
```

Flash 138,236 B; RAM 41,840 B + stack 4,096 B; UF2 276,480 B. `firmware/redpoint_pico/build/redpoint_reva.uf2`.

### Verification remaining at implementation time

At implementation time, iPad Safari/Chrome display/touch/browser-chrome/zoom differences and serving the device-hosted page with this UF2 had not been checked. The original acceptance steps were: after installing the UF2, check toolbar connection/save, sensitivity/inversion controls, shortcut recording and portrait rotation. iPad display and touch operation have since been verified, as recorded above.
