<!-- awesome-plan project=zedbsd record=ws045p006 -->

# ws045-p006: awk の gawk 拡張

Phase ID: `ws045-p006`
Parent: [WS045](../ws.md)
Status: planned
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
