<!-- awesome-plan project=zedbsd record=ws046 -->

# WS046: GNU 互換の make（autotools の出力を実行できる範囲）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-13 夜 ユーザーが p014 を「満たしたとみなす」、p007 は T1-050 の証拠で cleared、p008 の coreutils の cross build は p013 で通った、p011 は p012・p014 で置き換え。p005 の規約の全文は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG002
Related Milestones: MG001
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

`userland/base` に独自実装（Zlib）の make を作り、automake・autoconf が生成した Makefile（と libtool）で package を guest で build・install できるようにする。範囲は POSIX make と automake の出力が使う GNU の機能。並列 build（`-j`）と jobserver、GNU make の全機能は範囲の外（Future Work F-007）。受け入れは expat・zlib・coreutils などを guest で `./configure && make && make install` できることと、host の GNU make との差分試験。

## ユーザーの決定

- 2026-09-24: 「GNU互換のmakeも必要です。GNU makeの全機能はいらないので、autotoolsの出力を実行できる程度には実装を進めましょう。サブプロセスの並列度を取得するような機能はいらないです。」
- 2026-09-25: coreutils は cross build（ユーザーの判断、p008）。HAL の rdmsr を `%gs:0` に（承認「承認待ちのHALの変更を許可します」、[Guardrail](../guardrail.md) の承認済みの差分の表、差分は [hal-percpu-gs.md](hal-percpu-gs.md) へ移す予定）。
- 2026-10-07: make をベータ3 へ（原文の抜粋「…make…」）。2026-10-13: 「WS046はベータ2に入れます。」
- 2026-10-13 夜: p014 は「満たしたとみなす」。
- 2026-10-08: 「規約の整形（各 WS の全文規約の Phase）はベータ3」（p005 は WS の外の一括の見直しへ）。
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- 設計（p001）: automake の出力と zlib は POSIX make と入れ子の変数参照で動き、GNU の関数・条件は coreutils の `GNUmakefile`（maintainer 用）だけが使う。`GNUmakefile` は読まない、`-j` は受け付けて無視、GNU make だと名乗らない。
- POSIX make の核（p002、差分試験 45/45）、automake の idiom と GNU の機能（p003、host・guest とも 91/91、host で expat の build・check・install が GNU make と同じ）。
- kernel の mkdir(2) の `.`・`..` を EEXIST に（p006、BUG-032）。
- guest での速さ: libc の allocator の `free()` と buffer cache の hash（p007、`xmlparse.c` の compile 513 → 5〜7 秒、BUG-033）、region の page の探索と reclaim の queue・HAL の `%gs:0`（p009、configure 129 → 96 秒）、private の file の mapping の page cache の扱い（p010、p012、p014: file fault 25 → 3.2 µs/page、configure 30〜33 秒）。BUG-027 の再計測（p015）。
- 実 package（p004、p008、p013）: expat・zlib は guest で最後まで、coreutils は libc に `<mntent.h>` の getmntent 系ほかを足して cross build・install が通り、guest で `df`・`stat`・`ls` などが動いた。expat の `runtests` 4932/4932。
- Noct の source の stamp を patch の SHA-256 で判定（p016、BUG-093）。

## 制限・移管

- `-j`・jobserver と GNU make の全機能は Future Work F-007。
- `make check` は bash が無い所で止まる物がある（p014 の記録）。
- 規約の全文の見直し（p005）は WS の外の一括の見直しへ。
- 差分試験（`tests/make-diff.py`・`make-guest.sh`・`cases/`）は `plan/tools/guest/make-cases.sh` が使うので `plan/tools/` へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws046-p001 | 調査と設計、差分試験の土台 | cleared（q400-i01） |
| ws046-p002 | POSIX make の核 | cleared（q401-i01、45/45） |
| ws046-p003 | automake の idiom と GNU の機能 | cleared（q403-i01、91/91） |
| ws046-p004 | 実 package の build（guest） | cleared（q404-i01、残りは p007・p008 へ） |
| ws046-p005 | 規約の全文確認と回帰 | planning のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws046-p006 | mkdir(2) の `.`・`..`（BUG-032） | cleared（q402-i01） |
| ws046-p007 | guest の clang の遅さ（BUG-033） | phase.md は uncleared。T1-050 の証拠で cleared（2026-10-13、ws.md の記録） |
| ws046-p008 | 実 package（expat・zlib・coreutils） | uncleared のまま WS を閉じた（coreutils は p013 で通った） |
| ws046-p009 | guest の configure の遅さ | cleared（q422-i01） |
| ws046-p010 | file の page の fault の複写を減らす | cleared（q419-i01） |
| ws046-p011 | p010 の実装と試験 | uncleared（戻した。p012・p014 で置き換え） |
| ws046-p012 | private の file の mapping の page cache の共有の設計 | cleared（q430-i01） |
| ws046-p013 | libc に mount の一覧の API | cleared（q424-i01） |
| ws046-p014 | private の file の mapping で cache の page を直接 map | phase.md は uncleared。2026-10-13 夜 ユーザー「満たしたとみなす」で cleared |
| ws046-p015 | BUG-027 の再計測 | cleared（2026-10-03 Q1、T1-034） |
| ws046-p016 | BUG-093 Noct の stamp | cleared（2026-10-03 Q1） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws046/` にある。Queue の履歴は [plan/history](../history/index.md)。
