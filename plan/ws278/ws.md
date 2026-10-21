<!-- awesome-plan project=zedbsd record=ws278 -->

# WS278 Raspberry Pi 5 対応

- Master: [plan/master.md](../master.md)。Status: incomplete（2026-10-21 開始）。Primary Milestone: MG008（異種機での実証）。Related: MG003。
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
| p003 | SMP（PSCI の CPU_ON、A76 の Aff1） | p002 | planning |
| p004・p004b | RP1 の USB（`pciex4_reset=0` で PCIe を引き継ぎ、DWC3 の xHCI、poll から）、RP1 の割り込み | p002 | planning |
| p005 | RP1 の Ethernet（Cadence GEM） | p004 | planning |
| p006 | WiFi（sdio2 の SDIO host、bcm43455 の流用） | p002 | planning |
| p007 | BT（uarta） | p006 | planning |
| p008 | 規約の全文の見直し | p002〜 | planning |
| （別 WS の提案） | GPU: V3D 7.1 と HVS6（design.md §4 D2） | p002 | — |

## 現在の状態・再開点

- 2026-10-21: P3（phase-runner、Opus 5.5）が q1542 で p001 から始めた。p001 は design.md（版 1）。p002 は image の生成まで（c7909625c は main 7547e49c4 に merge 済み、locore の再配置の stub は dba0bf700）。
- 再開点: 実機の試験（[tests/rpi5-hw-test.md](tests/rpi5-hw-test.md)）の結果を見て p002 を判定。ユーザーの判断 D1〜D3（design.md §4）。
