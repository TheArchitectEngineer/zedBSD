<!-- awesome-plan project=zedbsd record=ws204 -->

# WS204: HD Audio の録音（内蔵マイク）とスマホ経由の HFP 通話の音

<!-- awesome-plan-current:start -->
Status: incomplete・保留（ベータ3・4 の段は取り消し、何をやるかは Q1 とユーザーが整理中）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-27 ユーザー「なお、ベータ3，ベータ4という概念はキャンセルされました。…ベータ3でやること、ベータ4でやること、という内容は、今からQ1と私で整理していきます。」。元の段の出典: HFP の通話の音が目的（2026-10-11 ユーザー「HFPの実装(p006, p007)、HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。」）。2026-10-13 ユーザー「HFP 全体をベータ3」、Master の未決に「WS204・WS205 の扱い」）。旧: incomplete（2026-10-11 P1 q929: p001 の設計）。残り・制限: p001 の U1・U2、p002〜p005
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
| [p002](phase002/phase.md) | pci-hda: 入力の pin の選び直し（jack の sense）・VRef・boost・log、host の試験 | in-progress（実装・host 済み） |
| p003 | 確かめの道具 `audio-loop`（再生しながら録り 1 kHz を測る）、host の試験、QEMU（T1） | in-progress（2026-10-18 Q1: phase.md に合わせた。旧: planned） |
| p004 | 5330 の実機（T1 の `audio-loop`、ユーザーが WAV を聞く UAT） | planned |
| p005 | 規約の全文の見直し | planned |
