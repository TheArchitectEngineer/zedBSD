<!-- awesome-plan project=zedbsd record=ws086 -->

# WS086: ls の出力を GNU ls と同じにする

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（GNU ls との差の調査と設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

- 今の `userland/base/ls/main.c` は `-C` のときだけ列に並べ（幅は固定の 80）、既定は 1 行に 1 つ。
- GNU ls と同じにする: 出力が端末なら既定で列（`-C`、縦の順）、幅は端末の幅（`TIOCGWINSZ`、無ければ `COLUMNS`、無ければ 80）、
  端末でなければ 1 行に 1 つ。`-1`・`-x`・`-w` の扱い、列の幅の決め方（GNU の最小の列の数の計算）を GNU に合わせる。
- 色（`--color`）・`-F` などの GNU の拡張をどこまで含めるかは p001 で決める（既定の見た目に関わるものを優先）。
- POSIX の ls の振る舞い（WS001・WS043 の試験）を壊さない。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws086-p001 | GNU ls（coreutils）との差の一覧、列の配置の設計、含める option の範囲 | planning | — |
| ws086-p002 | 実装と host・guest の試験（GNU ls の出力との比較） | planning | p001 |
| ws086-p003 | 規約の全文との照合、回帰 | planning | p002 |
