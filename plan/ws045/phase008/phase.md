<!-- awesome-plan project=zedbsd record=ws045p008 -->

# ws045-p008: 実際の script と guest の回帰

Phase ID: `ws045-p008`
Parent: [WS045](../ws.md)
Status: planned
Queue: なし（サブエージェント）
依存: ws045-p002〜p007

## 目的

足した拡張を実際の script と guest（zedBSD の libc・TRE・/bin/sh）で確かめる。

## 範囲

- `plan/tools/utils/configure-diff.sh` を拡張を使う package（binutils の opcodes 以外で軽いもの、emacs の該当部分を抜いた script、ncurses、bash など）で走らせ、GNU の道具と生成物を比べる。
- GNU の case を `--export` で guest に出し、amd64 の guest（`plan/tools/sh/guest-diff.sh`、POSIXLY_CORRECT 無しで）で走らせる。
- 4 platform の build と boot test（`plan/tools/boot-test.sh`）。

## 受け入れ

- configure-diff が同一、guest の GNU の case が host と同じ結果、boot test が PASS。
