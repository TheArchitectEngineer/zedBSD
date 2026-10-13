<!-- awesome-plan project=zedbsd record=q1001 -->

# q1001 / WS074 Acid3 参照背景画像

Status: finished（2026-10-13 22:30〜22:35 JST、最大90分）
Approval: このチャットでユーザーが「p179の範囲を承認する」と回答。範囲は参照の `reference.png` が描かれない一般的な原因の調査・最小修正・同一 Acid3/Acid2 条件での再計測。選定時の exact scope は [WS074 Queue](../ws074/queue.md) と [p179](../ws074/phase179/phase.md)。

q1001-i01 / [ws074-p179](../ws074/phase179/phase.md): **cleared**。絶対配置の子だけを持つページでは root の高さが0。背景画像の位置決め領域が0の場合に描画を省く `paint/list.c` の判定を修正し、参照画像665×454が canvas 原点に描かれた。[回帰試験](../ws074/tests/host-canvas-background.c)は pass、host build warning 0、`host-image-zero` は10/10 pass。固定 WPT の Acid3 は得点100/100、inventory100/100、例外0、差 **56647/480000**（q1000 の302056から245409減）、Acid2 差 **1865/120000**（q1000から不変）。[report](../ws074/phase179/evidence/20261013-q1001/report.json) と Acid3/Acid2 並列画像・参照 layout/paint を保存。別 OS / QEMU は未実施。C 全文・component 規約と `git diff --check` を確認。

出力: p179 は cleared。p178 の q1000-i01 は uncleared の履歴として保持。Acid3 の残り56647差、Acid2 の1865差は残務。p100 の全画面差0・WS074受け入れは未達。次の p178 の1領域試行は候補であり、この Queue から自動で実装しない。独立 `codex/ws074-acid3` の source/試験変更は main 統合を別途照合する。GitHub Issue/Project 公開と push は保留。`master.md` は変更していない。
