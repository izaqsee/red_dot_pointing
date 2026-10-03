# 実装・確認事項ログ

日時はJST（UTC+09:00）。変更、未決事項、質問、回答、解決を追記で管理する。

## 2026-09-26 20:46:38 +09:00 — RP-001 感度transfer function（記録日時）

- 状態：実装・自動検証済み／実機評価待ち。
- 基準main：`6f83848b0bd3bdb878b55d28ce602a56b5175379`。
- 方向補正4項目、Wheel smoothstep response 2項目、v4保存とv1/v2/v3移行、Configuratorを追加。
- 検証：共通config・Pico/lwIP integration・UI66件・ARM build・mainとの差分scope検査成功。
- 既存問題：mainのscroll期待値はbase0.1導入前のまま。旧単位scale回帰と現行main出力比較を分離した。
- 旧milestone hash gateは現行mainに対しても古い。fixtureを更新せず、新しいscope比較を併設した。
- 相手先への質問案（未送信）：評価機での4方向補正、低速／高速scrollの操作感、OS間の極性は適切か。
- 未決事項：実機テスト結果と推奨gain。Pointer curveは今回対象外。
- 詳細：[設計・検証・実機確認事項](development/transfer-function.md)。
- upload/pushは実施していない。
