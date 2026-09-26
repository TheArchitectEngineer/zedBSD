<!-- awesome-plan project=zedbsd record=ws045p002 -->

# ws045-p002: grep の GNU 拡張

Phase ID: `ws045-p002`
Parent: [WS045](../ws.md)
Status: planned
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

実際の script と WS042 の試験が使う grep の GNU 拡張を足す。regex の拡張（`\|`・`\+`・`\<` など）は libc の TRE にあるので、ここは option と出力。

## 範囲

- 一致の形: `-w`（`--word-regexp`、GNU と同じく一致を右へずらして語の境界を探す）、`-o`（`--only-matching`、空の一致は出さない）、`-x` の long 形。
- 文脈: `-A N`・`-B N`・`-C N`・`-NUM`・`--context[=N]`、区切りの `--`、`--group-separator=S`・`--no-group-separator`。`-n` と名前の区切りは文脈の行で `-`。
- file: `-r`（`--recursive`、symlink は operand だけ辿る）・`-R`（`--dereference-recursive`）、operand が無ければ `.`、`--include=GLOB`・`--exclude=GLOB`・`--exclude-dir=GLOB`、
  `-h`/`--no-filename`・`-H`/`--with-filename`・`--label=NAME`、`-L`/`--files-without-match`、`-Z`/`--null`。
- 数: `-m N`/`--max-count=N`（それ以上読まない。文脈の後ろは出す）、`-b`/`--byte-offset`。
- 入力: `-z`/`--null-data`、binary（NUL を含む file）で「Binary file X matches」、`-a`/`--text`、`-I`、`--binary-files=TYPE`。
- long option: `--extended-regexp`・`--fixed-strings`・`--basic-regexp`（`-G`）・`--regexp=`・`--file=`・`--ignore-case`（`-y`）・`--invert-match`・`--count`・
  `--files-with-matches`・`--line-number`・`--quiet`/`--silent`・`--no-messages`・`--line-regexp`・`--color[=WHEN]`/`--colour`（色は付けない）・`--version`・`--help`。
- egrep・fgrep は既に別の program（`userland/base/egrep`・`fgrep`、`grep -E`/`-F` を exec）。変えない。

範囲外: `-P`（PCRE。使う script が調査で無かった。status 2 で拒否）、色付け、`--line-buffered` 以外の性能向けの option（`-U`・`--line-buffered` は受け付けて無視）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only grep` が全件一致。
- WS043 の POSIX の case（`util-diff.py`）が全件一致（492/492）。
- `style-check.py userland/base/grep/main.c` の findings 0。build（host）warning 0。

## 検証

host の差分試験だけ（guest は p008）。
