<!-- awesome-plan project=zedbsd record=ws206 -->

# WS206: Raspberry Pi 4 の WiFi（BCM43455、SDIO）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p003 cleared、p002a・p002b・p004・p005・p008 は実装・host 試験・build 済み（main）。残りは Pi 4 の実機の確かめ（p006）だけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG005
Related Milestones: —
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Raspberry Pi 4 の内蔵の WiFi（BCM43455、SDIO）の driver を書き、既存の networkd・wifi の道具で WPA2 の AP に接続できるようにする。完了の条件（p001 の設計）: Pi 4 で wlan0 が現れ（OTP の MAC）、`wifi search`・`wifi list` と Settings の Wi-Fi の頁に AP が出て、WPA2-PSK（AES）に firmware の supplicant で接続し、DHCP・ping、切断と再接続・Wi-Fi の切と入ができる。firmware は `userland/firmware/bcm43455/`（既定は無効）で license を監査済み。QEMU raspi4b の起動が壊れない。UAPI・HAL は不変。WPA3・PMF・省電力・DMA・50 MHz は範囲の外。

## ユーザーの決定

- 2026-10-11（由来）「RPi4のWiFiを実装。」
- 2026-10-11「推し通りです。」: E1 4-way handshake は firmware の supplicant（PMK は core が計算）、E3 WLAN の core に FullMAC の mode を足す。
- E4「FATにGPLのライセンスファイルを入れれば問題ないように思います。」（GPL-2 の license を package と同じ所に同梱）。
- E6「世界共通」→ driver が CLM の load の後に `country` を `XX` に設定し、読み戻した値を log に出す（NVRAM に ccode が無く、Cypress の license は file の改変を禁じるため）。
- 「ではあとは推奨でお願いします」（p003 の残りの判断）。
- 2026-10-12「…そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」
- 2026-10-27（完了の根拠、q1608 経由）「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- GPIO の helper（`rpi4-gpio.c`、mailbox の GPIO）、SDIO の host（`rpi4-sdio-host.c`）・SPI 126 の dispatcher・SDIO の card の層（`rpi4-sdio.c`）・probe（`rpi4-wifi.c`）、BCM43455 の bring-up（SDPCM・BCDC・event・bus の thread）、radio の op（scan・非同期の connect・errno・data・link loss・restart）、WLAN の core の FullMAC の engine（`wlan-fullmac.c`）を実装した（main）。
- firmware の package `bcm43455-firmware`（T1-564 PASS: rpi4 の image に 7 file、sha256 一致）。
- host 試験（ASan/UBSan・TSan、radio・radio-smp の scenario）と rpi4・amd64 の vmunix の build（warning 0）。
- country の revision は出典のある rev 0（世界共通 XX）を先に試し、断られたら -1（2026-10-13 Q1 の判断）。

## 制限・移管

- Pi 4 の実機の受け入れ（scan・WPA2・DHCP・ping・Settings）と T1 の QEMU raspi4b の起動の回帰は未実施。p006（実機）・p007（規約と値の出典の監査）は WS の外。
- Pi の実機の Wi-Fi の失敗は [BUG-311](../bugs/BUG-311.md)（scheduled）で追う。
- SDHCI の手続きの重複（`rpi4-sdhci.c` と `rpi4-sdio-host.c`）の統合は F-098。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws206-p001 | 調査と設計（W1〜W14、E2・E4・E6） | in-progress のまま WS を閉じた（第 3 版、E2（試験の AP）は未回答） |
| ws206-p002a | GPIO の helper と mailbox の GPIO | in-progress のまま WS を閉じた（実装・host・build 済み dc2504ff2、T1 は未実施） |
| ws206-p002b | SDIO の host・card の層・probe | in-progress のまま WS を閉じた（実装・host・build 済み、実機と T1 は未実施） |
| ws206-p003 | firmware の package と license の監査 | cleared（2026-10-13、T1-564） |
| ws206-p004 | Broadcom の bring-up、SDPCM・BCDC・event、wlan0 の公開と shutdown | in-progress のまま WS を閉じた（実装・host・build 済み、実機と T1 は未実施） |
| ws206-p005 | radio の op（scan・connect・data・link loss・restart） | in-progress のまま WS を閉じた（実装・host・build 済み、実機と T1 は未実施） |
| ws206-p006 | 実機の受け入れ（T1） | 未着手のまま WS を閉じた（実機の確認は未実施） |
| ws206-p007 | 規約の全文の見直しと値の出典の監査 | 未着手のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws206-p008 | WLAN の core の FullMAC の engine と station の分岐 | in-progress のまま WS を閉じた（実装・host・build 済み） |

## 記録の所在

Phase の計画・設計・値の出典の表（phase004/values.md）・結果は削除した。git の commit `fb1f6afee` の `plan/ws206/` にある。Queue の履歴は [plan/history](../history/index.md)。
