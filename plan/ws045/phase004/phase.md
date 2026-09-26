<!-- awesome-plan project=zedbsd record=ws045p004 -->

# ws045-p004: sed の script の GNU 拡張

Phase ID: `ws045-p004`
Parent: [WS045](../ws.md)
Status: planned
Queue: なし（サブエージェント）
依存: ws045-p003

## 目的

GNU sed の script の拡張を足す。binutils の configure（`\t`）、emacs の configure（`[\t ]`）など、実際の configure が使うものを先に。

## 範囲

- regex の前処理: bracket の中を含めて `\t`・`\n`・`\f`・`\v`・`\a`・`\r`、`\dNNN`・`\oNNN`・`\xHH`、`\cX`。`\``・`\'`（buffer の始めと終わり）。
- 置換: `\U`・`\L`・`\u`・`\l`・`\E`、`\n` と escape（`\t` など）。
- address: `0,/re/`、`first~step`、`addr,+N`、`addr,~N`、`/re/M`。
- s の flag: `M`（`^`・`$` が各行）、`e`（結果を command として走らせる）。
- command: `Q [N]`、`q N` と `Q N` の終了 status、`T label`、`F`、`z`、`W file`、`R file`、`e [command]`、`v [version]`、`l N`、
  `a`・`i`・`c` の一行形の escape（GNU は `a\ttext` の `\t` を tab にする）。
- ERE の後方参照（`-E` の `\1`、TRE が扱えるか確かめる）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only sed` が全件一致。WS043 の POSIX の case が全件一致。style-check 0、warning 0。
