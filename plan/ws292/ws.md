<!-- awesome-plan project=zedbsd record=ws292 -->

# WS292 GHCI の session の保護（PROT）を zedBSD の driver に実装する

- Master: [plan/master.md](../master.md)（2026-10-26 23:50 作成）。Primary Milestone: MG006。前提: [WS290](../ws290/ws.md)（GHCI 2.0 の driver）。
- Status: planning（2026-10-26 23:50 新設。WS290 の後）
- 出典: 2026-10-26 23:50 ユーザー（WS290 の U5「PROT を実装しない」への回答）「PROT は実装しませんが、WSを立てて、あとで実装します。」

## 単一の到達目標

GHCI の session の保護（docs/ghci.md の PROT の capability、2.0 での形は GHCI の設計者の判断を待つ。WS290 の [ghci-questions.md](../ws290/ghci-questions.md) G29）を、WS290 の driver に足し、host 試験で確かめる。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（2.0 の PROT の形の確認、WS290 の構成への足し方、host 試験） | WS290 の完了、G29 の回答 | planned |
