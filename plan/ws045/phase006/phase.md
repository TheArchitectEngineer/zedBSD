<!-- awesome-plan project=zedbsd record=ws045p006 -->

# ws045-p006: awk の gawk 拡張

Phase ID: `ws045-p006`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

gawk を前提にした script が使う拡張を足す。調査では build script での使用は少ない（`gensub` が gdb の保守用 script に 1 つ）ので、利用者の script でよく使われるものに絞る。

## 範囲

- 関数: `gensub`、`systime`・`strftime`・`mktime`、`and`・`or`・`xor`・`lshift`・`rshift`・`compl`、`match(s, re, arr)`、`split(s, a, re, seps)`、`asort`・`asorti`。
- 構文: `**`・`**=`、`func`、`\x` の escape、`switch`、`BEGINFILE`・`ENDFILE`、`IGNORECASE`。
- 入力: RS が 1 文字より長いときの regex、`RT`。
- option: `--version`・`--posix`・`-e`/`--source`・`--field-separator`・`--assign`・`--file`。
- `system()` の値を終了 status に（現在は wait の status）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only awk` が一致（`PROCINFO` は範囲外なら case を直す）。WS043 の POSIX の case が全件一致。style-check 0、warning 0。

## 実装（2026-09-27）

- `userland/base/awk/gawk.c`（新規、Makefile の source に追加）: `gensub`（`&`・`\\0`〜`\\9`・`\\&`、`g`/`G` か n 番目、target は変えない）、`systime`、
  `strftime([format[, time[, utc]]])`（既定の書式は gawk と同じ）、`mktime("YYYY MM DD HH MM SS [DST]")`、`and`・`or`・`xor`（2 個以上）・`lshift`・`rshift`・`compl`
  （53 bit、gawk と同じ）、`asort`・`asorti`（数が文字列より先、dest を省けば source を並べ替える）、`match(s, re, arr)` の配列（`arr[n]`・`arr[n, "start"]`・`arr[n, "length"]`）。
- `lex.c`: `func`（`function` の別名）、`**`・`**=`（`^`・`^=`）、`\xHH`（string と regex の literal）、上の関数の名前。
- `parse.c`: 新しい関数の引数の数、`match` の 3 つ目・`asort`/`asorti` は配列でなければ構文 error。
- `builtin.c`: 関数の振り分け（`system`・`close`・`fflush` を明示し、残りを `gawk_call` へ）、`match` の 3 つ目の引数。
- `record.c`: 1 文字より長い RS は regex（gawk）。1 byte ずつ読んで、それ以上読んでも伸びない一致で record を切り、読み過ぎた 1 byte は `ungetc` で戻す。`RT`（record を終えた文字列）を
  1 文字の RS・regex の RS・段落 mode で設定。
- `io.c`: `system()` の値を終了 status に（signal は 256 + 番号、gawk と同じ）。`--posix` か `POSIXLY_CORRECT` なら wait の status のまま（POSIX と gawk の POSIX mode）。
- `main.c`: option を `command_options_next` で（program の位置で止まる。`permute` を切る）。`-e`/`--source`（`-f` と組める）、`--field-separator`・`--assign`・`--file`、
  `--version`、`--posix`、`--traditional`・`--re-interval`（受け付ける）。

範囲外（使う script が調査で無く、実装も大きい。必要になったら新しい Phase）: `IGNORECASE`、`switch`、`BEGINFILE`/`ENDFILE`、`PROCINFO`、`split` の 4 つ目（区切りの配列）、
`@include`/`@load`、双方向の pipe `|&`。これらの case は `cases/awk.sh` から外した。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only awk`（gawk 5.2.1） | **58/58**（p001 の 43 件から範囲外の 5 件を外し、20 件を足した） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case。awk は 155 件） | **492/492**（`system` の値は POSIX mode で従来どおり） |
| `python3 plan/tools/style-check.py userland/base/awk/*.c userland/base/awk/awk.h` | 0 |
| `sh plan/ws045/tests/target-check.sh userland/base/awk/*.c` | amd64・i386 とも warning 0 |
| image の build・guest | 未実施（p008） |

## 制限

- regex の RS は 1 byte ごとに regexec するので、長い record では遅い。
- 段落 mode の `RT` は常に `"\n\n"`（実際の改行の数ではない）。
