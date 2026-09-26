<!-- awesome-plan project=zedbsd record=ws045p003 -->

# ws045-p003: sed の option の GNU 拡張

Phase ID: `ws045-p003`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

`sed -i`（10 package）ほか、GNU sed の option を足す。

## 範囲

- `-i[SUFFIX]`・`--in-place[=SUFFIX]`: file ごとに同じ directory の一時 file へ書き、mode を保って rename。SUFFIX があれば元を `NAME SUFFIX` に残す（`*` を含めば名前に置き換える）。
  file ごとに行番号と `$` が始まり直す（`-s` を含意）。`w /dev/stdout` は本当の標準出力。読めない file は message の後で次へ（status 2）。
  symlink は既定で置き換え、`--follow-symlinks` で辿った先を書き換える。
- `-s`/`--separate`、`-z`/`--null-data`（行の区切りが NUL）、`-E`/`-r`/`--regexp-extended`、`-n`/`--quiet`/`--silent`、
  `-e`/`--expression=`、`-f`/`--file=`、`-l N`/`--line-length=N`（`l` の幅）、`-u`/`--unbuffered`、`--posix`（受け付ける）、`--debug`（受け付けて無視）、
  `--sandbox`（`e`・`r`・`w` を拒む）、`--version`・`--help`。
- option と operand の混在（`sed -i FILE -e SCRIPT`、GNU の getopt の順の入れ替え）。`POSIXLY_CORRECT` があれば最初の operand で止める（POSIX）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only sed` の option の case が一致（script の拡張は p004）。
- WS043 の POSIX の case が全件一致。style-check 0、warning 0。

## 実装（2026-09-27）

- `userland/base/sed/main.c`: option を `command_options_next`（ws045-p002 で `userland/base/common/command.c` に足した GNU の流儀の読み取り）で読む。
  `-n`/`--quiet`/`--silent`、`-e`/`--expression`、`-f`/`--file`、`-E`/`-r`/`--regexp-extended`、`-i[SUFFIX]`/`--in-place[=SUFFIX]`（`-ie` は suffix が `e`、GNU と同じ）、
  `-s`/`--separate`、`-z`/`--null-data`/`--zero-terminated`、`-l N`/`--line-length`、`-u`/`--unbuffered`、`--posix`、`--sandbox`、`--follow-symlinks`、
  `--debug`（受け付けて注釈は書かない）、`--version`、`--help`。operand の後の option（`sed -i FILE -e SCRIPT`）。`-i` で file が無ければ「no input files」で status 4（GNU）。
- `sed.h`: `struct sed_settings`（main.c が埋め、execute.c が読む）。`sed_execute` の引数に。`struct sed_command` の `number_given`（`l N`）、`struct sed_program` の `sandbox`。
- `execute.c`:
  - `-s`（`-i` が含意）: 先読みは file の終わりを越えず（`$` が各 file の最後の行、空の file は飛ばす）、次の cycle で `next_file` が次の file へ進み、行番号と
    範囲（`in_range`）を始め直す。hold space は続く。
  - `-i`: file ごとに同じ directory の一時 file（`mkstemp`、mode と owner を写す）へ出力し、file の最後の行の後（か `q` で終わるとき）に
    suffix があれば元を backup の名前へ、一時 file を元の名前へ rename。suffix の `*` は base name に（`/` を含めば cwd からの path、無ければ file の directory）。
    `--follow-symlinks` は `realpath` の先を書き換え、無ければ symlink を普通の file に置き換える（GNU）。通常の file でない物は「couldn't edit X: not a regular file」
    で status 4、読めない file は status 2 で次へ。`w /dev/stdout` は本当の標準出力。
  - `-z`: 行の区切りを NUL に。入力、出力（自動の出力、`p`、`=`、`l`、`a`/`i`/`c`、`w` の file）、`N`・`G`・`H` の継ぎ目、`D`・`P` の探す区切り。
  - `N` が最後の行で: GNU の既定は pattern space を書いて script を終える。`--posix` か `POSIXLY_CORRECT` なら書かずに消す（従来の POSIX の動き）。
    `n` と `N` は `-s` では次の file へ続く（GNU と同じ）。
  - `l`: 幅は `l N` の数か `-l`（既定 70）。0 は折らない。
  - `-u`: 入力を unbuffered で開き、cycle ごとに出力を flush。
- `compile.c`: `--sandbox` で `r`・`w`・`s///w` を「e/r/w commands disabled in sandbox mode」で拒む（`e`・`R`・`W` は p004）。`l N` の数を `number_given` に。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only sed`（GNU sed 4.9） | 103/129。落ちる 26 件は全て p004 の範囲（script の拡張）。option の case（p001 の 26 件と足した 36 件）は全件一致 |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case） | **492/492** |
| `python3 plan/tools/style-check.py userland/base/sed/*.c userland/base/sed/sed.h` | 0 |
| `sh plan/ws045/tests/target-check.sh userland/base/sed/*.c` | amd64・i386 とも warning 0 |
| image の build・guest | 未実施（p008） |

## 制限

- `-z` の `a`・`i`・`c` の一行形の text の終わりは、GNU では改行になる場合と NUL になる場合がある（書き方で違う）。ここは常に NUL。case に入れていない。
- `--debug` の注釈は書かない。
