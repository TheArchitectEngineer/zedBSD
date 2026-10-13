<!-- awesome-plan project=zedbsd record=ws228 -->

# WS228: 独自実装の簡単な vi を base に足す

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 p001 実装済み・merge 済み、T1 待ち）
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
| [p001](phase001/phase.md) | 範囲の設計（design-reviewer）→ 実装・host 試験 | in-progress（q971、P3。実装・host 試験 PASS・review-1 反映済み。cleared は p002 の T1 の後に Q1 が判定） | — |
| p002 | T1: guest の端末で編集・保存（T1-595。試験 B は p004 の後） | planned（T1-595 の試験 A は 5 段 FAIL、原因は試験の serial の読み手と比べ方。vi-guest.py を直し、再試験の依頼は [p001 §11a](phase001/phase.md)） | p001、p004 |
| [p003](phase003/phase.md) | 規約の全文の見直し | in-progress（2026-10-14 P3。vi の全 .c・tty-csi・tty.c の WS228 の変更・host 試験を読み、書き方を直した。style-check 207 件 → 0、build warning 0、host-vi 44 項目・tty-csi PASS。agent/p3 の WIP 5af1a70db。cleared は Q1 の判定） | p001、p004（merge 済み。p002 の T1 とは独立に実施） |
| [p004](phase004/phase.md) | kernel の VT console の CSI に引数 2 つの `H` と最小限の `m`（review-1 F1、2026-10-14 ユーザー「kernel の console を直す」） | in-progress — merge 済み（33c96c435）、T1-596 待ち | — |
