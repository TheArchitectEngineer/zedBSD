<!-- awesome-plan project=zedbsd record=ws045p005 -->

# ws045-p005: build script が使う他の utility の GNU 拡張

Phase ID: `ws045-p005`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

p001 の調査で configure・Makefile が使うと分かった、sed・grep・awk 以外の GNU 拡張を足す。

## 範囲

- `sort -V`/`--version-sort`（emacs configure）、`-h`/`--human-numeric-sort`、sort の long option。
- `head -n -N`・`-c -N`（最後の N を除く）、`head`/`tail` の `--lines`・`--bytes`（WS042）。
- `cmp -i SKIP[:SKIP2]`・`--ignore-initial`（binutils・gcc・gdb の configure）、`--silent`/`--quiet`、`--verbose`、`--bytes`。
- `touch --reference=FILE`（binutils opcodes の configure）、`-d`/`--date`（GNU の日付の書式のうち ISO の形と `@秒`）、`--no-create`。
- `date -d`/`--date=`（`@秒`、ISO 8601 の日付と時刻）、`-r FILE`/`--reference`（file の mtime）、`-R`、`-I`。
- `find -maxdepth`・`-mindepth`・`-iname`・`-ipath`・`-print0`・`-delete`・`-empty`・`-executable`・`-readable`・`-writable`・`-regex`・`-iregex`、`-printf`（`%p`・`%f`・`%h`・`%s` などの主なもの）。
- `readlink -f`・`-e`・`-m`・`-n`、`stat -c`/`--format`/`--printf`、`/bin/echo -e`・`-E`、`expr` の keyword（`length`・`match`・`substr`・`index`・`+`）。

範囲外（WS001 の POSIX の台帳にあるもの）: `xargs`（POSIX の option 自体が無い。POSIX 2024 の `-0`・`-r` も WS001 の #152）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only misc` の上の範囲の case が一致。WS043 の POSIX の case が全件一致。
- 触った file の style-check が増えない（新しい関数は 0）、warning 0。

## 実装（2026-09-27）

共通: `userland/base/common/command.c` に GNU の日付の読み取り `command_parse_date`（`@秒[.小数]`、`YYYY-MM-DD`・`YYYYMMDD`、`hh:mm[:ss[.小数]]`（`T` か空白の後）、
zone（`Z`・`UTC`・`GMT`・`+hhmm`・`+hh:mm`）、`now`・`today`・`yesterday`・`tomorrow`、`[+-]N 単位 [ago]`（second〜year、fortnight、複数形）、単位だけ（1）。
zone の無い日付は local time か UTC（呼び手が選ぶ））。option は全て `command_options_next`（p002）。

| utility | 足したもの |
| --- | --- |
| `sort` | 型 `V`（GNU の version 順: 接尾辞 `.tar.gz` などを外して比べ、数字の並びは数、`~` は終わりより前、`.`・`..`・隠し名が先）・`h`（単位 K〜Q、負の単位）・`g`（strtod、数でない物が先）・`M`（月名）、`-s`（入力順を保つ）、`-z`、`--sort=WORD`、`--check[=quiet]`、`-S`・`-T`・`--parallel`（受け付けて無視）、long option 全般 |
| `head` | `-n -N`・`-c -N`（最後の N を除く。ring で保持）、数の単位（`b`・`kB`・`K`・`KiB`・`MB`・`M` …）、`-q`・`-v`、`-z`、`-NUM`、`--lines`・`--bytes` ほか |
| `tail` | 数の単位、`-q`・`-v`、`-z`、`-F`（`-f` として）、`--lines`・`--bytes`・`--follow[=…]` ほか。`-s`・`--retry`・`--pid` は受け付けて無視 |
| `cmp` | `-i SKIP1[:SKIP2]`/`--ignore-initial`（binutils・gcc・gdb の configure）、`-n`/`--bytes`、`--silent`・`--quiet`・`--verbose`。POSIX の出力（`char`）は変えない（WS001 の ws001-p017 の試験がそのまま通る） |
| `touch` | `-d` の GNU の形（POSIX の形で読めなければ `command_parse_date`）、`--reference`・`--date`・`--no-create`・`--time=atime\|mtime`、`-f`・`-h`（受け付ける） |
| `date` | 書き直し: `-d`/`--date`、`-r`/`--reference`（file の mtime）、`-u`/`--utc`、`-R`、`-I[spec]`、書式は `strftime`（UTC、従来と同じ）に GNU の `%N`・`%s` を足す。時刻の設定は従来どおり無い（POSIX の残りは WS001 #30） |
| `find` | `-maxdepth`・`-mindepth`・`-regextype`・`-iname`・`-ipath`・`-wholename`・`-iwholename`・`-regex`・`-iregex`（path の全体に一致）・`-empty`・`-executable`・`-readable`・`-writable`・`-false`・`-amin`・`-cmin`・`-mmin`・`-delete`（`-depth` を含意）・`-printf`（`%p %f %h %P %s %m %M %d %y %u %g %U %G %T@ %A@ %C@ %%` と `\n \t \0 \\`）・`-quit`、`-and`・`-or` |
| `readlink` | 書き直し: `-f`・`-e`・`-m`（link を全て辿って絶対名に。存在の要求が違う）、複数の operand、`-n`・`-q`・`-s`・`-v`・`-z`、long option |
| `stat` | 書き直し: `-c`/`--format`・`--printf`（escape）・`-t`・`-L`、GNU の指示子（`%n %N %s %b %B %o %a %A %f %F %u %U %g %G %h %i %d %D %x %y %z %X %Y %Z %w %W %%`） |
| `/bin/echo` | GNU の option の語（`-n`・`-e`・`-E` の組み合わせ）。既定の escape の解釈（XSI）は builtin と同じまま。`POSIXLY_CORRECT` では builtin と同じ |
| `expr` | GNU の keyword `length`・`match`・`substr`・`index`・`+`（operand の位置で、要る operand が続くとき） |

範囲外: `xargs`（POSIX の option も無い。POSIX 2024 の `-0`・`-r` を含め WS001 の #152。case から外した）。`realpath` の GNU の option（調査で使われていない）。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only misc`（GNU coreutils 9.7・findutils 4.10） | 108/126。落ちる 18 件は全て p007 の範囲（cp・mv・rm・mkdir・ln・wc・cut・uniq・tr・basename・dirname・env・split）。p005 の case（足した 47 件を含む）は全件一致 |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only sort` | **24/24**（新規 `cases/sort.sh`） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case） | **492/492** |
| `sh plan/ws001/tests/run-cmp-host-test.sh`（WS001 の cmp） | PASS |
| style-check（新しいか書き直した file: sort・head・tail・touch・date・readlink・stat・echo） | 0（date は 11→0、readlink は 5→0、stat は 21→0） |
| style-check（legacy の file） | cmp 32→28、find 161→160、expr 115→115、command.c 23→23（増えていない） |
| `sh plan/ws045/tests/target-check.sh`（上の 12 file） | amd64・i386 とも warning 0 |
| image の build・guest | 未実施（p008） |

## 制限

- `date` と `stat` の `%x` などは、date が常に UTC で書くのは従来どおり（`-d` の日付も UTC で読む）。stat の時刻は local time。
- `find -regex` の既定の regextype は BRE（TRE の GNU 拡張つき）。GNU の emacs 型とは細部が違いうる。
- `expr substr`・`index` は byte で数える（`length` は文字）。
- `stat` の `%t`・`%T`（device の major・minor）は 0。
