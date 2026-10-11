<!-- awesome-plan project=zedbsd record=ws204 -->

# WS204: HD Audio の録音（内蔵マイク）とスマホ経由の HFP 通話の音

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-11 P1 q929: p001 の設計）
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
| [p001](phase001/phase.md) | 調査と設計（録音の経路は ws035 で既にある。足りないのは入力の選び方・mic bias・boost・log と実機の確かめ） | in-progress（設計済み、U1・U2 を Q1 へ） |
| p002 | pci-hda: 入力の pin の選び直し（jack の sense）・VRef・boost・log、host の試験 | planned |
| p003 | 確かめの道具 `audio-loop`（再生しながら録り 1 kHz を測る）、host の試験、QEMU（T1） | planned |
| p004 | 5330 の実機（T1 の `audio-loop`、ユーザーが WAV を聞く UAT） | planned |
| p005 | 規約の全文の見直し | planned |
