<!-- awesome-plan project=zedbsd record=ws139 -->

# WS139: desktop の速さの改善（まとめたチケット）

<!-- awesome-plan-current:start -->
Status: closed（2026-10-13 ユーザー「WS139はクローズします。」。旧: incomplete、Target はベータ3）
Primary Milestone: MG006
Related Milestones: MG003（実機の 5330）
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

2026-10-04 ユーザー:「F-071、F-072、F-070、はWSを立てて計画を作り、他の能力が低いセッションで処理できるようにしてください。」[F-072](../future-work.md)（2026-10-03 user「C5の200msは問題視しません。…あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」）の内容。散らばった速さの項目（C5 の最初の frame、WS094 の L3、WS127 の速さ、WS102 の手書き、BUG-143 の入力の遅れなど）を 1 つの台帳にまとめ、同じ道具・同じ環境で測り直してから直す。

## 結果（2026-10-13 close）

- 台帳（P-01〜P-12）と環境の印（E1: 開発の host の QEMU＋Venus（lavapipe）、E2: 5330 の host の i915 の Venus、E3: 5330 の i915 の passthrough、E4: 5330 の単独の起動）を作った。E1 の数字は E1 の前後の比べにだけ使い、目標の判定は E2 で行う。
- ユーザーの決定（2026-10-04）: U1 判定は E2、U2 5330 の操作は試験の担当、U3 Text Editor・Terminal の直の入力の目標は key から画面まで 50 ms 以内、U4 合成の費用（P-03）を最優先、U5 開発の host に後で NVIDIA の GPU を刺す。
- p001: 計測の image と一括の script、E1 の基準値（T1-176、2 回とも全指標に値）。E1 の主な値: 合成の frame の間隔 約 100 ms（約 4 回/秒）、C5 の最初の frame 1 回目 約 204 ms・2 回目から 95 ms、Text Editor の直の入力 中央値 307〜339 ms、Terminal 302〜306 ms。
- p002: E2 の手順と基準値（T1-284 PASS、5330 の Intel Iris Xe の Venus で E2=yes）。

## 制限・残り

- 台帳のレビュー（p003）と改善の Phase（p004 以降）・最後の規約と回帰は行っていない。速さの改善はこの WS では未達。
- 速さの項目は他の WS・bug に残る（WS094 の L3、WS127 の F4、WS102 の手書き、WS075 の C6、BUG-159 など）。まとめて扱う場合は Future Work（F-072 を promoted から戻すか新しい項目）の候補（勝手に移していない）。
- 計測の道具（`plan/ws139/tests/`）は master の Tools・試験の一覧に無く、他から参照されない（T1-176・T1-284 は実行済み）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws139-p001 | 計測の image と一括の script、台帳の表の道具、E1 の基準値 | cleared（2026-10-05 Q1、T1-176） |
| ws139-p002 | E2（5330 の host の i915 の Venus）の手順と基準値 | cleared（2026-10-07 Q1、T1-284） |
| ws139-p003 | 台帳のレビュー（直す項目・目標・順をユーザーと決める） | canceled（2026-10-13、ユーザーの指示で WS を close。旧: planned） |
| ws139-p004〜 | 改善の Phase（p003 で定める予定だった） | canceled（2026-10-13、同上。未定義） |
| 最後 | 全文規約の見直し、E1 の回帰、台帳の最終の値 | canceled（2026-10-13、同上。未定義） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
