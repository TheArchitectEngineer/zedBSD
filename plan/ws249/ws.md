<!-- awesome-plan project=zedbsd record=ws249 -->

# WS249: BCM43455（Raspberry Pi 4 の WiFi）の driver の出典の表記と規約の整理

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002 cleared（main 331d314be）。残りは Pi 4 の実機の WiFi の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[coding-style.md](../coding-style.md)、[WS248](../ws248/ws.md)（AX211 での同じ整理）、[WS215](../ws215/ws.md)（Pi 4 の Bluetooth、同じ chip）

## 目標

`src/drivers/wifi/bcm43455/`（21 file、約 10,400 行）の出典の表記を Guardrail に合わせ（file の先頭の出典を除き block ごとの「Value(s)/Procedure referenced:」、本文に出典の名前（OpenBSD・bwfm・WHD・Infineon・Linux・brcmfmac など）を書かない、出典と一致する symbol 名を付け直す）、開発の管理情報を comment・log から除き、plan/coding-style.md の全文に合わせる。動作は変えない。firmware の package（`userland/firmware/bcm43455/`）は対象外。

## ユーザーの決定

- 2026-10-15「bcm43455も同じガードレールで修正してほしいです。WSを立ててください。」（「同じガードレール」は WS248 の指示「引用は引用部分の直前でブロック単位で行う、ファイル先頭にまとめて引用元を書かない、referenced:形式で引用元を書く、あとはコーディングスタイルの修正が必要ですね。」）
- 2026-10-15「WS249では削除を行いましょう。」（開発の管理情報を comment・log から削除する）

## 結果

- p001: file の先頭の出典を除き block ごとの Value(s)/Procedure referenced: へ、本文の出典の名前の除去、出典と一致する symbol 名の付け直し、開発の管理情報の削除。
- p002: 規約の全文の見直し（不要な前方宣言、決まり文句の comment、条件の中の呼び出し、`?:` など）。
- 確かめ: build（rpi4 の config）warning 0、preprocess の後の比較で comment 以外の差は意図した物だけ（各 phase.md の記録）。main 331d314be。
- WHD の 2 file の path は不明のまま（ユーザー確認済み、p001 の結果）。

## 制限・移管

- 実機（Pi 4、10.0.30.2）の WiFi の確かめ（scan・WPA2 接続・通信）は未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws249-p001 | 出典の表記: file の先頭の出典を除き block ごとの Value(s)/Procedure referenced: へ、本文の出典の名前の除去、出典と一致する symbol 名の付け直し | cleared（2026-10-15、q1205、P2） |
| ws249-p002 | 規約の全文の見直し | cleared（2026-10-15、q1205、P2） |

## 記録の所在

Phase の計画・結果（`plan/ws249/phase001`・`phase002`）は削除する。git の commit `fb1f6afee` の `plan/ws249/` にある。Queue の履歴は [plan/history](../history/index.md)。
