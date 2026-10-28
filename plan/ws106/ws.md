<!-- awesome-plan project=zedbsd record=ws106 -->

# WS106: テスト用アプリを userland/tests/ に集約

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。fg013（test・見本を userland/tests へ）は 2026-10-01 達成。p002 の ime-probe は 2026-10-07 の make menuconfig の整理（ユーザー: desktop の下の試験の program は tree ごと tests へ）で userland/tests/ime-probe に移った）
Completed: 2026-10-27（q1608）
Primary Milestone: MG001
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

base・desktop のテスト用アプリと指定された見本を `userland/tests/` に移し、既存の選択・実行・デモの振る舞いを保つ。2026-10-01 ユーザーの[レビューコメント](../reviews/2026-10-01-review.md)が出典。対象は[対象表](inventory.md)の 30 件（base 21、desktop 9）と追加の 13 file。設計と移動の手順は [design.md](design.md)、台帳は [survey.json](survey.json)。

## ユーザーの決定

- 2026-10-01（q539）「13 ファイルも追加して移動する」→ base/tests 直下の syscall-smoke.c ほか 13 file を `userland/tests/` 直下へ。
- 2026-10-01「WS106 は移動に限定し、既存スタイルの維持を認める」→ [全文の限定例外](../standards/ws106-relocation.md)。
- 2026-10-02「IME の人間の作業が終わったので、ime-probe を試験の場所へ移す作業を再開してよいです。」
- 2026-10-07: make menuconfig の整理で、desktop の下の試験の program は tree ごと tests へ（ime-probe はこれで移った）。
- 2026-10-08「ベータ3にします：WS009・026・106 文書・試験の整理・試験アプリの集約、WS139 デスクトップの速さ」。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p003 の規約の全文は WS の外の一括の見直しへ。

## 結果

- p001: 30 package＋追加 13 file、全 166 tracked file の hash・mode・移動元 → 先と参照 217 file を棚卸しし、menu・package・config・install の互換と build・boot の手順を design.md に確定。
- p002（q540）: 29 package＋追加 13 file の 164 file を移した（hash・mode・参照・registry を保存）。Linux の GCC・Clang の clean build・install warning 0、zedBSD の 28 app・POSIX・loader・image warning 0、boot-test.sh の login の PNG。ime-probe は別の作業で `userland/tests/ime-probe` に移り、2026-10-09 に P1 が照合と build を記録（Q1 の判定で cleared）。
- 検証の記録は [verification-checkpoint.md](verification-checkpoint.md)。

## 制限・移管

- 規約の全文・最終の build・install・boot（p003）は WS の外の一括の見直しへ。
- 既存の menuconfig の fixture の Fonts の分類の欠落は BUG-129（移動の範囲の外）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws106-p001 | 対象・参照・build の契約を確定 | cleared（2026-10-01、q539） |
| ws106-p002 | source・データと参照を移動 | cleared（2026-10-09 Q1 の判定、30 件の照合と build） |
| ws106-p003 | 全文規約・build/install・最終 boot | planning のまま WS を閉じた（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果（`phaseNNN/`）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws106/` にある。台帳と checkpoint（`survey.json`・`checkpoint.json`・`*-before.txt` ほか）はこの directory に残す。Queue の履歴は [plan/history](../history/index.md)。
