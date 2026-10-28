<!-- awesome-plan project=zedbsd record=ws254 -->

# WS254 networkd: DHCP の失敗の後に 1 分おきに取り直す

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001（DHCP の失敗の後 60 s おきに down → up → dhcpc）を QEMU で確かめて main（5cb63ff2a）。実機は確かめだけ）
Completed: 2026-10-27（q1608）
Primary Milestone: MG001
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

有線（と管理する WiFi）の interface で DHCP が失敗したら、networkd が 1 分おきに interface を down → up → DHCP の順で取り直し、lease が取れたら止める。cable の抜き差し・`net up` の今の動きと、静的な設定の interface は変えない。

背景: 5330・RDNA 機で DHCP を取れずに ssh が届かず、ユーザーの手で network を直すことが続いた（BUG-326 の dhcpc の temp の残りは修正済み）。169.254 の割り当ては 2026-10-16 に外した。

## ユーザーの決定

- 2026-10-16 ユーザー「また、networkdは、DHCPが失敗したあと、1分おきにDHCPを試みる修正を入れるのがいいと思います。別なサブエージェントで実施してOKです。」
- 2026-10-16 ユーザー（Q1 経由）「DHCP の 1 分おきの取り直しですが、インタフェースのdown->up->DHCPという手順にしてください。でないとインタフェースのリセットが必要なケースに対処できないです。」

## 結果

- 判断は `managed-lan.c`（interface ごとの `retry_at`・`retry_pending`・`resetting`、DHCP の設定・UNCONFIGURED・cable あり・held down でない・管理中の interface だけ）、実行は networkd の `main.c`（`ifconfig IF down` → up → 既存の configure の手順）。down/up が起こす carrier の事象は reset の最中は状態を変えない。`net up`・設定の変更・`net down` は予約を消す。confirmed commit が armed の間は取り直さない。Wi-Fi は変えていない（DHCP の失敗を join の失敗として扱う既存の動き）。main 5cb63ff2a。
- host 試験: dhcp-retry 36/36 PASS、既存の managed-lan-host-test PASS。
- QEMU（2026-10-16、tree 1f22728f5＋変更）: DHCP の server が起動の 200 秒後から答える構成で `retry-qemu: PASS`（leased=y after 206s、resets=3、obtained=1）。

## 制限・移管

- 実機（5330・RDNA 機）での確認は未実施。QEMU の usb-net で `ifconfig down/up` が carrier の事象を出すかは見ていない（どちらでも動く設計）。
- 規約の全文の見直し（p002）は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、q1608 の扱い）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws254-p001 | 1 分おきの取り直し（毎回 down → up → DHCP）の実装と QEMU の確認 | in-progress のまま WS を閉じた（main 5cb63ff2a、host 試験・QEMU PASS、実機は未実施） |
| ws254-p002 | 規約の全文の見直し | 未実施（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws254/phase001`、`plan/ws254/tests/`）は削除する。git の commit `fb1f6afee` の `plan/ws254/` にある。Queue の履歴は [plan/history](../history/index.md)。
