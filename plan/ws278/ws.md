<!-- awesome-plan project=zedbsd record=ws278 -->

# WS278 Raspberry Pi 5 対応

- ID: ws278、Master: [plan/master.md](../master.md)、Primary Milestone: MG008（異種機での実証）。Related: MG003。
- Status: incomplete（2026-10-21 開始。p001 設計済み、p002 image まで、実機の確認待ち）
- 出典: 2026-10-21 ユーザー「もう1つサブエージェントを追加して、Raspberry Pi 5対応を行ってほしいです。config/ci/config-rpi5.mkを作成して、イメージを生成できるようにしてほしいです。必要ならGPUドライバの対応、WiFiドライバの対応を行ってほしいです。」「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」

## 目的と完了の条件

- `config/ci/config-rpi5.mk` で Raspberry Pi 5（BCM2712、Cortex-A76、RP1 の southbridge、VideoCore VII）の起動 image を作れる。
- 実機で起動して login できる（実機の試験はユーザーの手元の機材で。QEMU に raspi5 の machine は無い）。
- 必要に応じて GPU（V3D 7.1）・WiFi（CYW43455 系）・RP1 の Ethernet・USB の driver。
- 新しいコードは plan/coding-style.md の全文。HAL の API（include/hal/hal.h）の変更は差分ごとのユーザーの事前承認。
- 設計の review に Fable の design-reviewer は使わない（ユーザーの指示）。担当は Opus 5.5。

## 制約・参照

- 既存の rpi4（config/ci/config-rpi4.mk、BCM2711）の platform・driver を土台に、差を足す。
- Guardrail: plan/guardrail.md（driver の配置、drv_ の symbol、HAL）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（[design.md](design.md)）: BCM2712 と BCM2711 の差、Phase の分け方、試験の方法 | — | in-progress（2026-10-21 P3 q1542、設計を書いた、Q1 の判定待ち） |
| [p002](phase002/phase.md) | config/ci/config-rpi5.mk と platform の起動（console・timer・interrupt・memory）、SD の root、image の生成 | p001 | in-progress（2026-10-21 image まで済み、実機の確認待ち） |
| [p003](phase003/phase.md) | SMP（PSCI の CPU_ON、A76 の Aff1） | p002 | in-progress（2026-10-21 実装と host の確認済み、実機は一括の試験） |
| [p004](phase004/phase.md) | RP1 の USB（`pciex4_reset=0` で PCIe を引き継ぎ、DWC3 の xHCI、poll から） | p002 | in-progress（2026-10-21 実装と host の確認済み、実機は一括の試験） |
| p004b | RP1 の割り込み（MSI-X → MIP → GIC） | p004 | planning |
| p005 | RP1 の Ethernet（Cadence GEM） | p004 | planning |
| p006 | WiFi（sdio2 の SDIO host、bcm43455 の流用） | p002 | planning |
| p007 | BT（uarta） | p006 | planning |
| [p008](phase008/phase.md) | 規約の全文の見直し（第 1 部: HAL の bsp を bsp-rpi-common・bsp-rpi4・bsp-rpi5 に分け全文で書き直し、前倒し） | p002〜 | in-progress（第 1 部 2026-10-21 済み、rpi4 の QEMU 起動の回帰は T1 待ち） |
| （別 WS の提案） | GPU: V3D 7.1 と HVS6（design.md §4 D2） | p002 | — |

## 現在の状態・再開点

- 2026-10-21: P3（phase-runner、Opus 5.5）が q1542 で p001 から始めた。p001 は design.md（版 1）。p002 は image の生成まで（c7909625c は main 7547e49c4 に merge 済み、locore の再配置の stub は dba0bf700）。
- 再開点: 実機の試験（[tests/rpi5-hw-test.md](tests/rpi5-hw-test.md)）の結果を見て p002 を判定。ユーザーの判断 D1〜D3（design.md §4）。

- 2026-10-21: ユーザーの指示で HAL の bsp を分けた（p008 の第 1 部）。`src/hal/arm64/bsp-rpi-common/`（共通）・`bsp-rpi4/`・`bsp-rpi5/`（board ごと、写して書き直し）。arch 共通の file に `HAL_BOARD_` の分岐は無い。
- 2026-10-21 ユーザーの判断: D1 serial console は不要（既定 n）、D2 GPU は別 WS（[WS279](../ws279/ws.md)）、D3 手元は Pi 5 2GB（2026-10-21 購入、D0 の見込み）、HDMI モニター・USB keyboard・Debug Probe 付き USB serial あり。p003〜p008 は実機の試験なしで続けて実装し、最後にまとめて実機（ユーザー「テストは後回しにして実装してしまって、ビッグバンでテストしましょう。」）。P3 q1543。
