<!-- awesome-plan project=zedbsd record=ws259 -->

# WS259 RTL8822B の表（rtl8822b-tables.inc）を値の引用に書き換える

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 の表の書き換え（13 配列とも値が同一、merge ee2de1f9d）と license の package の外し、p002 の規約 cleared。残りは実機の WiFi の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

`src/drivers/wifi/rtl8822b/rtl8822b-tables.inc` の先頭の上流の著作権表示・抜粋の宣言・出典の一覧を外し、表を block ごとの `Values referenced:` の引用に書き換える（[Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」）。表の値（driver が書く register の値と順序）は変えない。AGENTS.md の「RTL8822B の `.inc` は license を分けるために独立させており、別の file のまま保つ」は守る。

## ユーザーの決定

- 2026-10-18（クリック）: WS258 の監査で rtl8822b-tables.inc の先頭に上流の著作権表示と抜粋の宣言があった件に「WS244 と同じく書き換える」。
- 2026-10-18（クリック）: 実機の WiFi の確かめは「実機は残して待つ」。
- 2026-10-19（クリック）: license の package は「外す」（WS244 の i915-driver と同じ。firmware の license は残す）。

## 結果

- p001: 表の書き換えと値の比べ（13 配列とも同一、全配列の byte の SHA-256 一致）、merge ee2de1f9d。license の package（`rtl8822b-tables`）を Makefile の自動の選びと license-components.json から外した。`userland/base/licenses/rtl8822b-tables/` は今の tree に無い（2026-10-28 に `git ls-files` で確かめた）。`rtl8822b-firmware` の license は残る。
- p002: 規約の全文の見直し（tables.inc の comment と名前）。amd64 の vmunix warning 0。
- rootfs の全体の build（staging の実物）は見ていない（一覧で確かめた、p001 の記録）。

## 制限・移管

- 実機の WiFi の確かめは未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws259-p001 | 書き換えと表の値の比べ、license の package の取り外し | in-progress のまま WS を閉じた（書き換え・比べ・build・package の取り外しは済み、merge ee2de1f9d。実機の WiFi は未実施） |
| ws259-p002 | 規約の全文の見直し（tables.inc の comment と名前） | cleared（2026-10-19 Q1） |

## 記録の所在

Phase の計画・結果（`plan/ws259/phase001`・`phase002`）は削除する。git の commit `fb1f6afee` の `plan/ws259/` にある。Queue の履歴は [plan/history](../history/index.md)。
