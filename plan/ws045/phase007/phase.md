<!-- awesome-plan project=zedbsd record=ws045p007 -->

# ws045-p007: file utility の long option と小さな拡張

Phase ID: `ws045-p007`
Parent: [WS045](../ws.md)
Status: planned
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

利用者の script がよく使う file utility の GNU の long option と小さな拡張を足す。build script での使用は少ない（調査）。

## 範囲

- `cp -v`・`-t DIR`・`-T`・`-a`・`--preserve`、`mv -v`・`-n`・`-t`、`rm -v`・`--force`・`--recursive`、`mkdir -v`・`--parents`・`--mode`、
  `ln -n`・`-r`・`-v`・`-t`、`basename -s`・`-a`・`-z`、`dirname` の複数 operand、`cut --complement`・`--output-delimiter`、`wc -L`、
  `uniq -i`・`-w`、`tr` の long option、`env -u`、`split -d`・`--additional-suffix`、`tee` の long option。

POSIX の不足が前提になるもの（`cp -R` の欠けなど）は WS001 の台帳に任せ、ここは GNU の拡張だけ。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only misc` の上の範囲の case が一致。WS043 の POSIX の case が全件一致。触った file の style-check が増えない。
