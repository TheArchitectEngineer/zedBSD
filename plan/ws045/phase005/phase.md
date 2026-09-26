<!-- awesome-plan project=zedbsd record=ws045p005 -->

# ws045-p005: build script が使う他の utility の GNU 拡張

Phase ID: `ws045-p005`
Parent: [WS045](../ws.md)
Status: planned
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
