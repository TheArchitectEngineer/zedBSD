<!-- awesome-plan project=zedbsd record=ws086 -->

# WS086: ls の出力を GNU ls と同じにする

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: main からの依頼（subagent、worktree `wt/ws086`）
Resume point: p002 の Terminal の画面での確認（[p002](phase002/phase.md) の Resume point）から
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
| [ws086-p001](phase001/phase.md) | GNU ls（coreutils）との差の一覧、列の配置の設計、含める option の範囲 | cleared（2026-09-29） | — |
| [ws086-p002](phase002/phase.md) | 実装と host・guest の試験（GNU ls の出力との比較） | in-progress | p001 |
| ws086-p003 | 規約の全文との照合、回帰 | planning | p002 |

## 判断の既定（p001、2026-09-29）

- locale に従う（GNU と同じ）。guest の session に `LANG` が無いので、端末では UTF-8 の名前が escape される。`LANG=C.UTF-8` の設定は
  範囲外で、main に判断を依頼した。
- 色（`--color`）は含めない（GNU の既定は色なし）。含めない option の一覧は [p001](phase001/phase.md)。
- option は GNU の順（operand の後も読む。`POSIXLY_CORRECT` で POSIX の順）。

## 試験

- [tests/tty-run.py](tests/tty-run.py): 指定の幅の擬似端末に出力をつないで byte のまま取る（host）。
