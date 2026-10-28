<!-- awesome-plan project=zedbsd record=ws248 -->

# WS248: Intel AX211 の driver の出典の表記と規約の整理

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002 cleared（main 285c55e39、T1-698・T1-700）。残りは 5330 の素の起動での接続（U9）の実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[coding-style.md](../coding-style.md)、[WS004](../ws004/ws.md)、[WS247](../ws247/ws.md)（nvrtx での同じ整理）

## 目標

`src/drivers/wifi/intel-ax211/` と `include/drivers/wifi/intel-ax211/` の出典の表記を Guardrail に合わせ（file の先頭の出典の一覧を除き、引用した値・手順は block の直前の「Value(s)/Procedure referenced:」、本文に出典の名前を書かない）、不要な前方宣言を除いて plan/coding-style.md の全文に合わせる。動作は変えない。

## ユーザーの決定

- 2026-10-15「Intel AX211ドライバが、ガードレールに沿っていない部分がありそうです。引用は引用部分の直前でブロック単位で行う、ファイル先頭にまとめて引用元を書かない、referenced:形式で引用元を書く、あとはコーディングスタイルの修正が必要ですね。intel-ax211.cには不要な関数宣言が多数ありますし。これもWSを作ってコードを整理しましょう。サブエージェントを1つ割り当ててください。」
- comment の開発の管理番号は WS248 の範囲の外として残した。ユーザー「WS248でも消してほしかったですが、また今度でいいです。」→ [WS251](../ws251/ws.md) で削除する。

## 結果

- p001（comment だけ、39 file）: file の先頭の出典の段落を除き、「Source(s):」を block の直前の「Value referenced:」（5）・「Values referenced:」（78）・「Procedure referenced:」へ。本文の出典の名前を除いた。
- p002（書き方だけ、全 44 file。intel-ax211.c を 2 file に分割）: 前方宣言の整理、条件の中の呼び出し・`?:`・Boolean の式の代入を規約の形に。style-check.py の指摘は 690 → 8（残りは短く対称な選択）、定型の comment 3,432 → 0。
- 確かめ: vmunix（config/ci/config-amd64.mk）の build rc 0・warning 0、AX211 の host 試験 6 本 PASS。main 285c55e39、T1-698・T1-700。

## 制限・移管

- QEMU・実機（5330 の素の起動での接続、U9）の確かめは未実施。
- 開発の管理番号の comment は WS251。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws248-p001 | 出典の表記: file の先頭の出典を除き、block ごとの Value(s)/Procedure referenced: へ。本文の出典の名前の除去 | cleared（2026-10-15、P2） |
| ws248-p002 | 規約の全文の見直し: 不要な前方宣言の削除と coding-style.md の全文（intel-ax211.c を 2 file に分割） | cleared（2026-10-15、P2） |

## 記録の所在

Phase の計画・結果（`plan/ws248/phase001`・`phase002`）は削除する。git の commit `fb1f6afee` の `plan/ws248/` にある。Queue の履歴は [plan/history](../history/index.md)。
