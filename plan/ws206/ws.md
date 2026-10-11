<!-- awesome-plan project=zedbsd record=ws206 -->

# WS206: Raspberry Pi 4 の WiFi（BCM43455、SDIO）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1 の予約の Queue）
Primary Milestone: MG005
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001（調査と設計）から。P1 が Phase を書き足す。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー（このセッション）: 「RPi4のWiFiを実装。」

## 目標

Raspberry Pi 4 の内蔵の WiFi（Broadcom/Cypress BCM43455、SDIO の EMMC2 とは別の SDHCI）の driver を書き、既存の networkd・wifi の道具で WPA2 の AP に接続できるようにする。

## 完了の条件（仮、p001 で確定）

Pi 4（10.0.30.2）で scan・WPA2 の接続・DHCP・ping が通る（実機）。firmware は userland/firmware に置き、license を監査する。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調査と設計（既存の code・hardware・規格、判断が要る点をユーザーへ） | planned |
