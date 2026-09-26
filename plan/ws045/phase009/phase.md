<!-- awesome-plan project=zedbsd record=ws045p009 -->

# ws045-p009: 全文の規約確認

Phase ID: `ws045-p009`
Parent: [WS045](../ws.md)
Status: cleared（2026-09-27）
Queue: なし（サブエージェント）
依存: ws045-p002〜p008

## 目的

WS045 で変えた全 source を `plan/coding-style.md` の全文と照らし、機械的な検査（`plan/tools/style-check.py`）と人の目の確認を記録する。

## 受け入れ

- 変えた file の style-check が 0（legacy の file は着手前より増えない）。全文の review の結果と例外を記録。build warning 0、差分試験が全件。

## 結果

### style-check（着手前の merge base `3f0b7c67` → 現在）

全 43 の C file（`git diff --name-only 3f0b7c67 -- '*.c' '*.h'`）を比べた。

| file | 着手前 | 現在 |
| --- | --- | --- |
| 新規: `awk/gawk.c`・`grep/grep.h`・`grep/search.c` | — | 0 |
| 書き直した file: `basename`・`date`・`mkdir`・`mv`・`readlink`・`split`・`stat` の main.c | 5・11・18・16・5・12・21 | 全て 0 |
| 元から 0 の file（awk の残り、cut、echo、env、grep/main.c、head、ln、rm、sed の 4 file、sh/builtins.c、sort、tail、touch、tr、uniq、wc、command.h） | 0 | 0 |
| `cmp/main.c` | 32 | 27 |
| `cp/main.c` | 127 | 114 |
| `find/main.c` | 161 | 154 |
| `tee/main.c` | 13 | 10 |
| `common/command.c` | 23 | 23 |
| `expr/main.c` | 115 | 115 |
| `src/libc/regex/regcomp.c`（musl の TRE。1 行の変更） | 443 | 443 |

legacy の file では、WS045 が足した行・変えた行（`git diff -U0 3f0b7c67` の + 側）に findings が 0 であることも確かめた。この Phase で直したもの:
`cmp`（operand の数の段落の comment）、`cp`（blank-after-brace と段落の comment）、`tee`（段落の comment）、`find` の `parse_or`/`parse_and`
（条件の中の呼び出しをやめ、`joined`/`stops` に取ってから判定する形に書き直した）。残る findings は全て WS045 より前の行で、この WS の範囲外。

### 人の目の確認（style-check が判断しない項目）

- 名前: 新しい関数・変数は完全な語（`command_options_next`、`options_find_long`、`line_delimiter`、`step_matches`、`backup_name` など）。`_local` や番号の名前は無い。
- comment: 新しい関数の前の comment は「何をするか」と例外（GNU と POSIX の違い、POSIXLY_CORRECT での振る舞い）を書く。段落の comment は処理の意図を書く。
- 関数の形: forward declaration を file の先頭に置き、goto は使わない。条件演算子は短い対称の選択だけ。
- 例外: `src/libc/regex/regcomp.c` は外部由来（musl の TRE）の file で、1 行の変更だけにとどめ、周りの形は変えない。
- 残した課題: `expr`・`find`・`cp`・`command.c` の legacy の findings（WS045 の前から。WS001 と規約の WS の範囲）。

### build と試験

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws045/tests/target-check.sh <変えた 39 の .c>`（amd64・i386・arm64、`-Wall -Wextra -Werror`） | 39 file、warning 0 |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin2 --cases plan/ws045/tests/cases --gnu`（GNU の case） | **515/515** |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin2`（WS043 の POSIX の case） | **492/492** |
| WS001 の試験: `basename-test.sh`・`dirname-test.sh`・`run-tee-host-test.sh`・`run-cmp-host-test.sh`・`run-base-command-host-test.sh` | 全て PASS |
| amd64 以外（i386 の pcat・pc98、arm64 の rpi4）の image の build | 未実施（compile だけは上の target-check で確かめた。image は amd64 の lean image だけ。[p008](../phase008/phase.md)） |
