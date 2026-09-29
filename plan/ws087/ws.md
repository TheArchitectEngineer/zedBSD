<!-- awesome-plan project=zedbsd record=ws087 -->

# WS087: /bin/sh の対話の行編集（矢印キーの履歴と Tab の補完）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001 cleared（2026-09-29）。p002（履歴の file）は sh だけで始められる。libedit の `stifle_history`（p002）と Tab の補完（p003）は
`userland/base/libedit/` を変える許可を main に依頼中。ユーザーへの質問（5330 で同じ窓でも上が効かなかったか）も依頼中。設計は [p001](phase001/phase.md)
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
| ws087-p002 | 矢印キーの履歴（BUG-103 の修正）: 履歴の file（`HISTFILE`、既定 `~/.sh_history`）の読み込みと追記、libedit の履歴の大きさを `HISTSIZE` に（`stifle_history`、libedit の許可が要る） | planning | p001 |
| ws087-p003 | Tab の補完（libedit に GNU Readline と同じ名前の補完の hook と一覧、sh に `complete.c`。libedit の許可が要る） | planning | p001 |
| ws087-p004 | 規約の全文との照合、回帰（WS042 の sh の試験） | planning | p002・p003 |
