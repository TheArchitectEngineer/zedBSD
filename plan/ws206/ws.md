<!-- awesome-plan project=zedbsd record=ws206 -->

# WS206: Raspberry Pi 4 の WiFi（BCM43455、SDIO）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1 の予約の Queue。2026-10-13 p001 の設計の第 2 版、design-reviewer 待ち）
Primary Milestone: MG005
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001 の design-reviewer と E2・E4・E6 の回答の後、p002（SDIO）・p003（firmware の package）・p008（core の FullMAC）から。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー（このセッション）: 「RPi4のWiFiを実装。」

## 目標

Raspberry Pi 4 の内蔵の WiFi（Broadcom/Cypress BCM43455、SDIO の EMMC2 とは別の SDHCI）の driver を書き、既存の networkd・wifi の道具で WPA2 の AP に接続できるようにする。

## 完了の条件（p001 の設計で確定）

Pi 4（10.0.30.2）の実機で、内蔵の BCM43455 の wlan0 が現れ（chip の OTP の MAC）、`wifi search`・`wifi list` と Settings の Wi-Fi の頁に AP が出て、WPA2-PSK（AES）の AP に接続し（firmware の supplicant）、DHCP・ping が通り、切断と再接続・Wi-Fi の切と入ができる。firmware の BLOB は `userland/firmware/bcm43455/`（既定は無効、immutable の上流から取得して確かめる）にあり license を監査済み。QEMU raspi4b の起動が壊れない。UAPI・HAL は不変。WPA3・PMF・省電力・DMA・50 MHz は範囲の外。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（W1〜W14、E2・E4・E6 をユーザーへ） | in-progress（第 2 版、design-reviewer 待ち） | — |
| p002 | SDHCI の register の層（SD card を移す、振る舞いは不変）、SDIO の層、GPIO・mailbox の GPIO の helper（WS215 と共有）、電源・clock・IRQ・claim。Pi で CMD5・CCCR・CIS、起動の回帰 | planned | p001 |
| p003 | firmware の package `bcm43455-firmware` と license の監査 | planned | p001、E4 |
| p004 | Broadcom の bring-up、SDPCM・BCDC・event、bus の thread、wlan0 の公開、値の出典の表 | planned | p002、p003 |
| p005 | driver の radio の op（scan・connect・firmware の supplicant・data・link loss・restart） | planned | p004、p008 |
| p006 | 実機の受け入れ（T1）: scan・WPA2・DHCP・ping・Settings、可能なら Bluetooth と同時 | planned | p005、E2 |
| p007 | 規約の全文の見直しと値の出典の監査 | planned | p006 |
| p008 | WLAN の core の FullMAC の形（kernel の内部の API）と host の試験（p004 と並べて進められる） | planned | p001 |

2026-10-13 P1: p001 の第 2 版で p004・p005 の中身を分け直し、core の FullMAC の部分を p008 として足した（番号は付け直さない）。

## 2026-10-11 ユーザー「推し通りです。」

E1: 4-way handshake は firmware の supplicant（PMK は core が計算）。E3: WLAN の core に FullMAC の mode を足す。E2（試験の AP の SSID・passphrase・帯域）は未回答。
