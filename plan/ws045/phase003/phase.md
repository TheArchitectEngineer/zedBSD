<!-- awesome-plan project=zedbsd record=ws045p003 -->

# ws045-p003: sed の option の GNU 拡張

Phase ID: `ws045-p003`
Parent: [WS045](../ws.md)
Status: planned
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
