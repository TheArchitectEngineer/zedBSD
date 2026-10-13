<!-- awesome-plan project=zedbsd record=ws206 -->

# WS206: Raspberry Pi 4 の WiFi（BCM43455、SDIO）

<!-- awesome-plan-current:start -->
Status: incomplete（p001 は第 3 版の review-3 と E2・E4・E6 の回答の後、clearance は Q1 の判断待ち。p002a は実装・host・build 済み（dc2504ff2）、T1 待ち。p002b in-progress（2026-10-13 P1、実装・host・build 済み、Pi の実機と T1 待ち））（2026-10-11 Q1 作成、P1 の予約の Queue）
Primary Milestone: MG005
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p002a・p002b は実装・host 試験・build 済み（agent/p1）。p002b は Pi の実機で CMD5・CCCR・CIS の dmesg の行を読み（Q1 がユーザーに依頼、[p002b](phase002b/phase.md) の「Pi 4 で見る dmesg の行」）、T1 の QEMU raspi4b の起動の回帰の後に clearance。次は p003（firmware の package）・p008（core の FullMAC の engine）。p004 の前に shutdown の口の要否を Q1 へ。
p004 への申し送り（2026-10-13 Q1、ws206-p003 の P3 の判断 2）: E6「世界共通」→ NVRAM に ccode・regrev が無く、CLM に world の `XX` があり、Cypress の license は file の改変を禁じるので、driver が CLM の load の後に `country` を `XX` に設定し、読み戻した値を log に出す（p001 の「firmware の既定のまま」の推奨を置き換える。p004 の W4 にこの手順を足す）。
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
| [p001](phase001/phase.md) | 調査と設計（W1〜W14、E2・E4・E6 をユーザーへ） | in-progress（第 3 版、design-reviewer 待ち） | — |
| [p002a](phase002a/phase.md) | GPIO の helper（`rpi4-gpio.c`）と mailbox の GPIO の helper（`rpi4-firmware.c` に追記）、platform の初期化から GPIO の init、host の試験。**ws215-p003 がこれに依存**（名前と引数は p001 の W3） | in-progress（実装・host・build 済み dc2504ff2、T1 の QEMU の起動の回帰待ち） | p001 |
| [p002b](phase002b/phase.md) | SDIO の host（`rpi4-sdio-host.c`、独立の file）、SPI 126 の dispatcher（`rpi4-mmc-irq.c`）、SDIO の card の層と DT の slot（`rpi4-sdio.c`）、probe と thread（`rpi4-wifi.c`）、SD の base の記録は `rpi4.c`（`rpi4-sdhci.c` は不変）、pin と電源。Pi で CMD5・CCCR・CIS、Pi と QEMU の起動の回帰 | in-progress（実装・host・build 済み、Pi の実機と T1 待ち） | p002a（BUG-295 への依存は review-3 の指摘 7 で外した） |
| [p003](phase003/phase.md) | firmware の package `bcm43455-firmware` と license の監査（E4: GPL-2 を同梱、E6: world） | cleared（2026-10-13 Q1、T1-564 PASS） | p001、E4 |
| p004 | Broadcom の bring-up、SDPCM・BCDC・event、bus の thread と lock、wlan0 の公開と shutdown、値の出典の表 | planned | p002b、p003 |
| p005 | driver の radio の op（scan の順、非同期の connect と firmware の supplicant、errno の表、data、link loss、restart） | planned | p004、p008 |
| p006 | 実機の受け入れ（T1）: scan・WPA2・DHCP・ping・Settings、可能なら Bluetooth と同時 | planned | p005、E2 |
| p007 | 規約の全文の見直しと値の出典の監査 | planned | p006 |
| p008 | WLAN の core の FullMAC の engine（`wlan-fullmac.c`）と station の分岐（kernel の内部の API）と host の試験（p004 と並べて進められる、6〜8 LW） | planned | p001 |

2026-10-13 P1: p001 の第 2 版で p004・p005 の中身を分け直し、core の FullMAC の部分を p008 として足した（番号は付け直さない）。第 3 版で p002 を p002a（helper、WS215 と共有）と p002b（SDIO の host）に分けた（review-2 と Q1 の判断）。計 約 31〜33 LW。

依頼（Q1 へ）: [WS215](../ws215/ws.md) の p003 の依存に ws206-p002a を足す。`plan/master-design-policy.md` の「WLAN protocol-state ownership」に FullMAC の方針（p001 の W6.3 の末尾）を記録する。

## 2026-10-11 ユーザー「推し通りです。」

E1: 4-way handshake は firmware の supplicant（PMK は core が計算）。E3: WLAN の core に FullMAC の mode を足す。E2（試験の AP の SSID・passphrase・帯域）は未回答。
