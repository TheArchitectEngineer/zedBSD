<!-- awesome-plan project=zedbsd record=ws207 -->

# WS207: Raspberry Pi 4 のオーディオ（再生と録音）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1 の予約の Queue）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001（調査と設計）から。P1 が Phase を書き足す。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー（このセッション）: 「RPi4のオーディオを実装。（再生、録音）」

## 目標

Raspberry Pi 4 の音の出力（HDMI の音と、3.5mm の PWM の出力）と録音（USB の audio class の入力など、実機の構成で決める）を audiod から使えるようにする。

## 完了の条件（仮、p001 で確定）

Pi 4 で Music の再生の音が出る、録音した音が取れる（実機、ユーザーの確認）。どの出力・入力を対象にするかは p001 の調査で決めて記録する。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（既存の code・hardware・規格、判断が要る点をユーザーへ） | in-progress（設計の第 1 版、F1〜F3 待ち） |
| p002 | BCM2711 の DMA と PWM の音（3.5 mm の jack） | planned |
| p003 | USB の audio class（UAC1）の録音（ws197-p007a1 の後） | planned |
| p004 | HDMI の音（VCHIQ の音の service） | planned |
| p005 | 実機の UAT と規約の全文 | planned |
