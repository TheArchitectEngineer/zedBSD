<!-- awesome-plan project=zedbsd record=ws204 -->

# WS204: HD Audio の録音（内蔵マイク）とスマホ経由の HFP 通話の音

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1 の予約の Queue）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001（調査と設計）から。P1 が Phase を書き足す。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー（このセッション）: 「HFPの実装(p006, p007)、HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。AACはlibmediaにデコードを実装済み。」

## 目標

HD Audio（Intel HDA、5330）の録音の経路（ADC・入力の pin・capture の stream）を driver と audiod に実装し、WS197 p007 の HFP の通話で、内蔵のマイクとスピーカーで実際に通話できるようにする。

## 完了の条件（仮、p001 で確定）

5330 の内蔵マイクの音が audiod の capture で取れる（host の試験と実機）。WS197 の HFP の通話で、相手に声が届き、相手の声が内蔵のスピーカーで聞こえる（実機、ユーザーの確認）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調査と設計（既存の code・hardware・規格、判断が要る点をユーザーへ） | planned |
