<!-- awesome-plan project=zedbsd record=ws087 -->

# WS087: /bin/sh の対話の行編集（矢印キーの履歴と Tab の補完）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001・p002・p003・p005・p006 cleared（2026-09-29）。次は p004（規約の全文との照合、WS042 の sh の試験の回帰）。
実機（5330）の確認手順は main が預かった（[p002](phase002/phase.md)）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「/bin/shが上下キーでヒストリーをたどれないようです。たどれるようにしたいです。」
「/bin/shがタブキーの補間を使えません。使えるようにしたいです。」

- 対話の `/bin/sh`（Terminal・console・ssh）で、上下の矢印キーで履歴をたどれる（[BUG-103](../bugs/BUG-103.md)）。
- Tab で補完できる: 行の最初の語は命令（builtin・alias・関数・`PATH` の実行 file）、それ以外は path。候補が複数なら共通の部分まで、
  2 回目の Tab で候補の一覧（bash に近い振る舞い）。
- 既存の vi mode（ws042-p008）と POSIX の sh の振る舞い（WS042 の試験）を壊さない。vi mode の残り（[F-006](../future-work.md)）はこの WS の外。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws087-p001](phase001/phase.md) | BUG-103 の原因（line editor が有効になる条件、Terminal の送る escape）と、Tab の補完の設計 | cleared（2026-09-29。同じ shell の中の矢印は QEMU の Terminal・ssh・serial で動く。shell ごとに履歴が空から始まる（`HISTFILE` が無い）のが有力な原因） | — |
| [ws087-p002](phase002/phase.md) | 矢印キーの履歴（BUG-103 の修正）: 履歴の file（`HISTFILE`、既定 `~/.sh_history`）の読み込みと追記、libedit の履歴の大きさを `HISTSIZE` に（`stifle_history`） | cleared（2026-09-29。host 17/17、QEMU の ssh で新しい session から前の命令を呼べる。PS/2 の矢印が kernel で捨てられる件を発見し main へ） | p001 |
| [ws087-p003](phase003/phase.md) | Tab の補完（libedit に GNU Readline と同じ名前の補完の hook と一覧、sh に `complete.c`） | cleared（2026-09-29。host 29/29、QEMU の ssh と Terminal で補完と一覧） | p001 |
| [ws087-p005](phase005/phase.md) | PS/2 keyboard の E0 の key（矢印・Home・End・Delete ほか）が capability に無く捨てられる（BUG-103 の第 2 の原因、`ps2-8042.c`） | cleared（2026-09-29。PS/2 だけの QEMU で evdev と Terminal に届く、USB の回帰なし、boot test） | p002 |
| [ws087-p006](phase006/phase.md) | 既定の prompt で `$HOME` を `~` に（2026-09-29 ユーザー「ホームディレクトリにいるときにプロンプトに /home/kei と表示されるので、これを ~ にできるようにしたいです。」） | cleared（2026-09-29。host 8/8、QEMU の ssh と Terminal で `~`・`~/docs`） | — |
| ws087-p004 | 規約の全文との照合、回帰（WS042 の sh の試験） | planning | p002・p003 |
