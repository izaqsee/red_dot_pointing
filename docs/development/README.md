# Development documentation

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

公開向けの概要・試し方は[RedPoint README](../../README.md)を参照してください。
ここには旧READMEの開発・保守向け記述を移動しています。Arduino版・旧UIの履歴は各document冒頭で適用範囲を示しています。

| Document | 内容 |
| --- | --- |
| [Build / upload](build-upload.md) | Pico SDKへの入口、Arduino CLI/VS Code、COM自動検出helperの仕様・制限・テスト |
| [Configurator](configurator.md) | ローカル配信、HTTP/Serial、自動接続・permission、Recorder詳細 |
| [GitHub Pages](github-pages.md) | workflow、GitHub設定、権限・公開手順 |
| [Validation](validation.md) | 自動テスト環境、接続・保存・ボタン割当の実機チェックリスト |
| [Maintenance](maintenance.md) | Flash/migration、通信・エラー処理、LED、privacy、将来案・未実装事項 |

### Source of truth

- [Protocol / schema / persistent record](../protocol.md)
- [USB Ethernet / lwIP integration](../usb-ethernet.md)
- [Status LED validation](../status-led-validation.md)
- [Pico SDK target / build](../../firmware/redpoint_pico/README.md)
- [C.2設定分離・migration](../../firmware/redpoint_pico/MILESTONE_C2.md)
- [C.3 compact UI・検証結果](../../firmware/redpoint_pico/MILESTONE_C3.md)

READMEの概要・Hardware・License情報は公開READMEへ残しました。
その他の旧READMEの各詳細sectionは上記5 documentへ本文を移動し、相対リンクだけを移動先に合わせています。
古い記述は削除せず、現行仕様との相違を各document冒頭で明示しています。

## EN

See the [project README](../../README.md) for the public introduction and quick start.
These documents preserve development and maintenance material moved out of the README. Each document identifies historical Arduino / UI descriptions and their scope.

| Document | Contents |
| --- | --- |
| [Build / upload](build-upload.md) | Pico SDK entry point, Arduino CLI / VS Code, COM helper behavior, limitations, and tests |
| [Configurator](configurator.md) | Local serving, HTTP/Serial, auto-connect, permissions, and recorder details |
| [GitHub Pages](github-pages.md) | Workflow, repository settings, permissions, and deployment |
| [Validation](validation.md) | Test environment and hardware connection/storage/button checklists |
| [Maintenance](maintenance.md) | Flash/migration, communication/errors, LED, privacy, future work and unsupported features |

### Source of truth

- [Protocol / schema / persistent record](../protocol.md)
- [USB Ethernet / lwIP integration](../usb-ethernet.md)
- [Status LED validation](../status-led-validation.md)
- [Pico SDK target / build](../../firmware/redpoint_pico/README.md)
- [C.2 settings separation / migration](../../firmware/redpoint_pico/MILESTONE_C2.md)
- [C.3 compact UI / validation](../../firmware/redpoint_pico/MILESTONE_C3.md)

The overview, hardware, and license information remain in the public README. The original detailed sections were moved into the five guides above with relative links adjusted. Historical descriptions are retained and their differences from current behavior are identified at the top of each guide.
