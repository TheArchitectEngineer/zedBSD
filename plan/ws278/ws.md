<!-- awesome-plan project=zedbsd record=ws278 -->

# WS278 Raspberry Pi 5 対応

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: Pi 5 の image で起動し desktop・USB の mouse が動く（WS279 の 2026-10-22 のユーザーの確かめ）。D1〜D3 は 2026-10-21 ユーザーが回答済み。BT は 2026-10-22 ユーザー「BTはPi4でも動作していないため、いったんドライバを無効にしてください。」で無効）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: MG003
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

`config/ci/config-rpi5.mk` で Raspberry Pi 5（BCM2712、Cortex-A76、RP1 の southbridge、VideoCore VII）の起動 image を作り、実機で起動して login できる。必要に応じて WiFi・RP1 の Ethernet・USB の driver（GPU は [WS279](../ws279/ws.md)）。既存の rpi4 の platform・driver を土台に差を足す。設計は [design.md](design.md)。

## ユーザーの決定

- 2026-10-21「もう1つサブエージェントを追加して、Raspberry Pi 5対応を行ってほしいです。config/ci/config-rpi5.mkを作成して、イメージを生成できるようにしてほしいです。必要ならGPUドライバの対応、WiFiドライバの対応を行ってほしいです。」「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」
- 2026-10-21「D1: serialは不要です。 D2: 分けましょう。 D3: Pi5 2GBです。…」→ D1 serial console は不要（既定 n）、D2 GPU は別 WS（WS279）、D3 手元は Pi 5 2GB（HDMI モニター・USB keyboard・Debug Probe 付き USB serial あり）。
- 2026-10-21「テストは後回しにして実装してしまって、ビッグバンでテストしましょう。」（p003〜p008 は実機の試験なしで実装し、最後にまとめて実機）
- 2026-10-21 HAL の bsp を分ける指示（p008 の第 1 部: `src/hal/arm64/bsp-rpi-common/`・`bsp-rpi4/`・`bsp-rpi5/`、arch 共通の file に `HAL_BOARD_` の分岐は無い）。
- 2026-10-22「Pi5のUATです。カーネルは起動しますが、USB, LAN, SDIOなどいろいろ使えないようです。」「まだだめですね」（1・2 回目の UAT）。
- 2026-10-22「BTはPi4でも動作していないため、いったんドライバを無効にしてください。」
- 2026-10-22（5 回目の UAT、1 分ごとの SSH の確認 40 回全部成功）「安定しているようなので、40回で切り上げて合格にしましょう。」
- 2026-10-22「Pi5のファンが回ってないんですが」→ p009。2026-10-23「ファンが回転していますね。」

## 結果

- p001 設計（design.md 版 1）。p002 `config/ci/config-rpi5.mk` と platform の起動（console・timer・interrupt・memory）、SD の root、image の生成（c7909625c、locore の再配置の stub dba0bf700）。p003 SMP（PSCI の CPU_ON、A76 の Aff1）。p004・p004b RP1 の USB（PCIe の引き継ぎ、DWC3 の xHCI）と割り込み（MSI-X → MIP → GIC）。p005 RP1 の Ethernet（Cadence GEM）。p006 WiFi（sdio2 の SDIO host、bcm43455 の流用）。p007 BT（uarta、config で無効）。p008 規約の全文の見直し（第 1 部 HAL の bsp の分離、第 2 部 p003〜p007 の C）。p009 ファンと温度（RP1 の PWM1、AVS の温度を hw.thermal に）。
- UAT の直し（2026-10-22、実機）: arm64 の HAL の SError の扱い、init の異常終了、firmware が渡す root port の bus 番号 0 と outbound の窓（device tree の ranges から毎回設定し、RP1 の BAR を並べ直す）、chip の stepping を boot flash の pin の mux の register から決める（C1 の device tree で D0 の chip）、Wi-Fi の cfg と supplicant の確かめを `sup_wpa` の iovar に、LAN の deadlock の直し（9d78dc950）、device tree の local-mac-address（bd4744521、main 4cc0e5001）。
- 実機（Pi 5 D0）: 5 回目の UAT で LAN・USB keyboard・RP1・Wi-Fi の SDIO の bus が動作、1 分ごとの SSH 40 回全部成功で合格（ユーザー）。Wi-Fi は wlan0（98:fe:54:22:65:c4）で elecom-84a0c3-5G に自動接続・DHCP 192.168.2.156・ping 20/20。ファンは 58 °C で level 1（2352 rpm）、60.6 °C で level 2（3877 rpm）、54.5 °C で level 1 に戻り、ユーザーが目視で確認。
- T1: QEMU raspi4b の Pi 4 の起動 PASS（Pi 4 の回帰、2026-10-22）。arm64 virt は config が無く未実施。

## 制限・移管

- BT は無効（ユーザー）。
- Wi-Fi の rx の glom の取りこぼしの log 1 回、切断・再接続、Pi 4 での Wi-Fi の直しの確かめは未実施。
- [BUG-345](../bugs/BUG-345.md)（Pi 5 の desktop でマウスのクリックが時々効かない）。
- 各 Phase の実機の行ごとの確かめは未実施（UAT の通しで見た）。Pi 4・Pi 5 の SDIO host の SDHCI の core への乗せ替えは Future Work fw-sdio-host-split。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws278-p001 | 調べと設計（BCM2712 と BCM2711 の差、Phase の分け方、試験の方法） | in-progress のまま WS を閉じた（design.md 版 1、D1〜D3 はユーザーが回答） |
| ws278-p002 | config-rpi5.mk と platform の起動、SD の root、image の生成 | in-progress のまま WS を閉じた（実機で起動・login、UAT 合格） |
| ws278-p003 | SMP（PSCI の CPU_ON、A76 の Aff1） | in-progress のまま WS を閉じた（実装と host の確認、実機は UAT の通し） |
| ws278-p004 | RP1 の USB（PCIe の引き継ぎ、DWC3 の xHCI） | in-progress のまま WS を閉じた（実機で USB keyboard が動く） |
| ws278-p004b | RP1 の割り込み（MSI-X → MIP → GIC） | in-progress のまま WS を閉じた（実装と host の確認、実機は UAT の通し） |
| ws278-p005 | RP1 の Ethernet（Cadence GEM） | in-progress のまま WS を閉じた（実機で LAN・SSH 40 回） |
| ws278-p006 | WiFi（sdio2 の SDIO host、bcm43455 の流用） | in-progress のまま WS を閉じた（実機で接続・DHCP・ping） |
| ws278-p007 | BT（uarta） | in-progress のまま WS を閉じた（ユーザーの指示で無効） |
| ws278-p008 | 規約の全文の見直し（HAL の bsp の分離、p003〜p007 の C） | in-progress のまま WS を閉じた（第 1・2 部済み、rpi4 の QEMU の起動の回帰 PASS） |
| ws278-p009 | ファンと温度 | in-progress のまま WS を閉じた（実機でファンの回転をユーザーが確認） |

## 記録の所在

Phase の計画・結果と実機の試験の手順（`plan/ws278/phase001`〜`phase009`、`plan/ws278/rpi5-hw-test.md`）は削除する。git の commit `fb1f6afee` の `plan/ws278/` にある。Queue の履歴は [plan/history](../history/index.md)。
