<!-- awesome-plan project=zedbsd record=ws045p002 -->

# ws045-p002: grep の GNU 拡張

Phase ID: `ws045-p002`
Parent: [WS045](../ws.md)
Status: cleared
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

## 実装（2026-09-27）

- `userland/base/common/command.c`・`command.h`: GNU の流儀の option の読み取り `command_options_start`・`command_options_next`（`struct command_options`・`struct command_long_option`）。
  文字の連なり（`-abc`）、値の付いた文字（`-e X`・`-eX`）、`::` の付いた文字だけの値（sed の `-i.bak` 用）、long option（`--name=value`・`--name value`・一意な略記、
  名前だけ違う同じ option（`--color`/`--colour`）は曖昧としない）、`-NUM`、`--`。operand の後の option も読み（GNU の順）、`POSIXLY_CORRECT` があれば最初の
  operand で止まる（POSIX の順）。operand は argv[1] から順に集める（割り当て無し）。command.o は全 platform の basic program に link 済みなので build の変更は要らない。
  zedBSD の libc の `getopt_long` は operand の後の option を読まないので使わなかった。
- `userland/base/grep/`: `main.c`（option、operand、`-r` の walk）と `search.c`（入力、一致、出力）に分け、`grep.h` を足した（Makefile の source の一覧も）。
  - 入力は descriptor から 32 KiB ずつ読む（pipe は来た分から処理）。NUL を含む入力は binary: 一致する行を書かずに「grep: NAME: binary file matches」を標準エラーへ
    （GNU grep 3.11 と同じ）。`-a`/`--binary-files=text`、`-I`/`--binary-files=without-match`。`-z` は NUL が行の区切り。
  - 一致: 全 pattern の leftmost-longest（`-o` の順）。`-x` は行の全体、`-w` は GNU と同じく一致が語でなければ同じ位置からの短い一致、次に先の一致を試す。
    行の途中からの一致は `REG_NOTBOL`、`-w` の短い一致だけ `REG_STARTEND`（TRE では部分の複製になるため最小限に）。
  - 出力: `-o`、`-b`、`-n`、名前（`-H`/`-h`、operand が複数か `-r` で見つけた file）、`-Z`、文脈（`-A`・`-B`・`-C`・`-NUM`、group の区切り `--`・
    `--group-separator`・`--no-group-separator`、別の file の間も区切る）、`-m`（後ろの文脈は次に選ばれる行の手前まで）、`-c`・`-l`・`-L`・`-q`。
  - `-r`/`-R`: operand の directory（`-r` は operand の symlink だけ辿る）、operand が無ければ `.`（名前に `./` を付けない）、`--include`・`--exclude`・`--exclude-from`
    （operand には名前の後ろの部分にも当てる）・`--exclude-dir`、directory の中の device・FIFO・socket は飛ばす。`-d skip`・`-d recurse`・`-D`。
  - long option は GNU grep 3.11 の名前（上の範囲）。`--color` は受け付けて色を付けない。`-P` は「Perl matching not supported」で status 2。`-U`・`-u`・`-T` は受け付けて無視。
- `src/libc/regex/regcomp.c`（TRE、1 行）: ERE の `\1`〜`\9` を後方参照に（glibc と同じ。POSIX は ERE の `\digit` を未定義としている。従来は数字そのものだった）。
- `plan/ws045/tests/target-check.sh`（新規）: 変えた C の file を amd64・i386 の zedBSD の target の flag（`-Wall -Wextra -Werror`、main の tree の sysroot の header）で compile だけする。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `sh plan/tools/utils/build-host-utils.sh build/ws045/bin` | status 0 |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only grep`（GNU grep 3.11） | **98/98**（p001 の 9/64 から。case を 35 足した。`-P` の case は範囲外として削った） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case） | **492/492** |
| `python3 plan/tools/style-check.py userland/base/grep/*.c userland/base/grep/grep.h` | 0 |
| `python3 plan/tools/style-check.py userland/base/common/command.c` | 23（着手前と同じ。足した関数は 0） |
| `sh plan/ws045/tests/target-check.sh userland/base/grep/main.c userland/base/grep/search.c userland/base/common/command.c src/libc/regex/regcomp.c` | amd64・i386 とも warning 0 |
| host の `-Wall -Wextra` の build | grep・command.c の warning 0 |
| 200 万行の `grep -c '99\+7'` | 0.68 秒（書き直し前の grep は 0.80 秒。GNU grep は 0.04 秒） |
| image の build・guest | 未実施（p008） |

## 残り・制限

- 行の途中から探す一致（`-o` の 2 つ目以降）では、`\<`・`\b` が直前の文字を見ない（TRE に部分の前の文脈を渡す手段が無い）。GNU とずれうる。
- `--color=always` でも色は付けない。
