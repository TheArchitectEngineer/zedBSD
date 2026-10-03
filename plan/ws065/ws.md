<!-- awesome-plan project=zedbsd record=ws065 -->

# WS065: `/bin/sh` に POSIX が未規定とする bash 拡張を足す

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: none
Resume point: —（2026-09-27 完了）
<!-- awesome-plan-current:end -->

## 目標

2026-09-26 ユーザー指示「/bin/shの互換性をまず向上させてもらえますか？また、stderrのリダイレクトなどで、bash拡張だがGNU/Linuxでは一般に使われていて、BSD系の/bin/shでは動かないような例はありますか？もし/bin/shに実装しても逸脱にならないなら、この機会に実装してしまうのがいいと思います。」

GNU/Linux の `/bin/sh` script がよく使う bash 拡張のうち、POSIX（XCU 2 章、Issue 8）が結果を未規定とする、または構文の誤りとする書き方だけを、bash と同じ意味で実装する。POSIX に合う script の意味は変えない。

## 結果

`userland/base/sh/` に次を bash と同じ意味で足した（POSIX の意味の dash との差分は変わらない）。

| 種類 | 足したもの |
| --- | --- |
| 構文（p001） | `$'...'`、`[[ ]]`、`function`、`(( ))`、`for (( ;; ))`、算術の `++`・`--`・`,`、`\|&`、`<<<`、`>& file`、`n>&m-`、`<( )`・`>( )`、UTF-8 の locale での `${#s}` の文字数 |
| 展開（p002） | `${v:o}`・`${v:o:l}`、`${v/p/r}`・`${v//p/r}`・`${v/#p/r}`・`${v/%p/r}`、`${v^}`・`${v^^}`・`${v,}`・`${v,,}`、`${!v}`（`$@`・`$*` にも） |
| builtin（p003） | `source`（と `.` の operand）、`let`、`test ==`、`declare`・`typeset`（`-i -l -u -r -x -g -p -f -F`）と `local` の同じ option、`printf -v`・`%q`、`builtin`、`pushd`・`popd`・`dirs` |

POSIX との関係で足さなかったもの（逸脱になる）: `&>`・`&>>`、`{a,b}`・`{1..3}`、`echo -e`、`$RANDOM` など bash の特別な変数。配列 `a=(...)` は後回し（[F-018](../future-work.md)）。

検証（最後の p004、2026-09-27）: bash を参照にする case 32/32、dash との差分試験 host 1438/1458（前と同じ失敗）、guest 1413/1458（変更前の sh と失敗の一覧が同一）、guest の expat の configure の生成物が変更前の sh と同一、`make -j4` と `tests/runtests` 4932/4932、boot test PASS。style-check 0（`userland/base/sh/` 全体と `builtin-standalone.c`）。

## 制限・移管

- bash と違う点（残す）: `(( undef++ ))` と `set -u`、C locale の `\u`、`$'\c''`、`${!#}` は最後の引数、`declare -a`・`-A`・`-n` は not supported、`declare -l -u` は `-u` が残る（bash 5 は両方を落とす）、読み取り専用への代入の status 2。記録は各 Phase（git の履歴）と [F-018](../future-work.md)・[F-019](../future-work.md)。
- `diff <(..) <(..)` の `/dev/fd` の不具合は BUG-054（WS067 で resolved）。
- 実機は未実施（QEMU だけ）。

## Phase 一覧

Phase の記録は完了に伴い削除した（git の履歴に残る）。

| Phase | 内容 | Status |
| --- | --- | --- |
| ws065-p001 | 構文の拡張 | cleared（q451-i01） |
| ws065-p002 | 展開の拡張 | cleared（q452-i01） |
| ws065-p003 | builtin の拡張 | cleared（q453-i01） |
| ws065-p004 | 規約の適合（style-check 0、host・guest の差分試験・expat・boot test が前と同じ） | cleared（2026-09-27、サブエージェント） |

## 試験と道具

- `plan/tools/sh/cases/bash-extensions.sh`（bash を参照にする 32 件）、`cases/arithmetic.sh`、`sh-diff.py` の `BASH_REFERENCE`。
- p004 で足した: `plan/tools/sh/build-guest-sh.sh`（この tree の sh を guest image の `libc.so` に対して作る）、`guest-batches.sh` の `GUEST_SH=`、`guest-expat.sh`（guest で expat の configure・make・runtests をある sh で走らせ、生成物を比べる）。
