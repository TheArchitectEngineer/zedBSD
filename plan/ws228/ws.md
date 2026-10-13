<!-- awesome-plan project=zedbsd record=ws228 -->

# WS228: 独自実装の簡単な vi を base に足す

<!-- awesome-plan-current:start -->
Status: planned（2026-10-14 Q1 作成）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q971（p001、P3）
Target: ベータ2（2026-10-14 ユーザー「ここまでがベータ2ですね。」）
Resume point: p001。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「あと、独自実装の簡単なviコマンドをbaseに追加します。ここまでがベータ2ですね。」

## 目標

- `userland/base/vi/` に zedBSD 独自の C の vi（外部の source を取り込まない）を足し、`/bin/vi` に入れる。menuconfig の Base に `vi`。
- 範囲は「簡単な」vi: POSIX の vi の基本（移動・挿入・削除・yank と put・undo・検索・`:w`・`:q`・`:wq`・`:q!`・`:e`・行番号への移動）。ex の全部・macro・複数の window などは入れない。範囲の案は p001 の設計で Q1 に出し、ユーザーの判断が要る点は印を付ける。
- 端末は libcurses（base の curses）か termios の直書き。UTF-8 の表示と編集（日本語の幅）を扱う。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 範囲の設計（design-reviewer）→ 実装・host 試験 | planned（q971、P3） | — |
| p002 | T1: guest の端末で編集・保存 | planned | p001 |
| p003 | 規約の全文の見直し | planned | p002 |
| [p004](phase004/phase.md) | kernel の VT console の CSI に引数 2 つの `H` と最小限の `m`（review-1 F1、2026-10-14 ユーザー「kernel の console を直す」） | planned（q973、P1） | — |
