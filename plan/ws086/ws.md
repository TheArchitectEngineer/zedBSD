<!-- awesome-plan project=zedbsd record=ws086 -->

# WS086: ls の出力を GNU ls と同じにする

<!-- awesome-plan-current:start -->
Status: completed（2026-09-29）
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし（サブエージェント、worktree `ws086-ls`）
Resume point: なし（完了）。残りの GNU との差は [F-055](../future-work.md)
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

## 結果

`userland/base/ls/main.c` を書き直した（2026-09-29、サブエージェント、main へ merge）。
- 既定の format: 端末なら `-C`（縦の順の列）、pipe なら 1 行に 1 つ。`-1`・`-C`・`-x`・`-m`・`-l` は最後が勝つ。
- 幅: `-w`・`--width` → 端末の幅（`TIOCGWINSZ`）→ `COLUMNS` → 80。tab の幅 `-T` → `TABSIZE` → 8。列は GNU の `calculate_columns` と同じく
  列ごとの幅、列の間は tab。
- 名前: 端末では shell-escape の quote と揃え（GNU の quote の分類を含む）、`-N`・`-q`、`-i` の右寄せ、`-F` の印（`-lF` も）。
- option: `command_options`、GNU の順（operand の後の option）と長い形、`POSIXLY_CORRECT`。
- 比較で見つけて GNU に合わせたもの: directory への symbolic link の operand の列挙、`-R` の見出し、operand の directory の測り方、
  印字できない文字を含む名前の幅。
- **決定（ユーザー、2026-09-29）**: 「我々のOSはutf-8のみをサポートしており、Escapeは不要と思います。」→ locale に関わらず名前を UTF-8 として扱い
  （`setlocale(LC_CTYPE, "C.UTF-8")`）、表示できる UTF-8 の文字は escape しない。C locale の GNU との差として残す。
- 色（`--color`）は入れない（GNU の既定も色なし）。

## 検証

- host: `plan/tools/ls/compare-gnu.py`（GNU ls 9.7 と byte 単位、7 つの tree × 37 の option の組 × 端末の幅と pipe × `C`・`C.UTF-8` ほか）で
  3362 件、既知の差 48 件（上の決定 24、`-L` の dangling link 24）を除いて差 0。`util-diff.py`（WS001・WS043 の POSIX の case を含む）1080/1080。
  build は warning 0、規約の全文と目視で照合（style-check 0 件）。
- QEMU（amd64）: `plan/tools/ls/guest-compare.py` 180 件で差 0、Terminal の画面、boot test PASS。
- 実機: 未実施。

## 制限・移管

- 残りの GNU との差（`-t` が秒、`-h` の丸め、`-L` の dangling link、message の文、不明な option の終了状態）と追加の option は [F-055](../future-work.md)。
- 範囲外の観察: `ls_time` は UTC（GNU は localtime）、`read_entries` の配列の拡大の桁あふれの検査が無い（旧 ls から）。
- `ssh -tt` で最後の命令の出力が欠ける件は [BUG-106](../bugs/BUG-106.md)（ls の外）。

## Phase

| Phase | 目的 | Status |
| --- | --- | --- |
| ws086-p001 | GNU ls との差の一覧（D1〜D13）と設計 | cleared（2026-09-29） |
| ws086-p002 | 実装と GNU との比較（host 3362 件・guest 180 件） | cleared（2026-09-29） |
| ws086-p003 | 規約の全文との照合、回帰 | cleared（2026-09-29） |

Phase の記録は完了の時に削除した（git の履歴にある）。試験は `plan/tools/ls/` に移した。
