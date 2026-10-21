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
| p001 | 調べと設計: BCM2712 と BCM2711 の差（boot の firmware・config.txt・device tree、interrupt controller、UART、timer、MMU の page、PCIe と RP1、SD・eMMC、USB、Ethernet、HDMI と framebuffer、V3D 7.1、WiFi・BT の接続）、Phase の分け方、試験の方法 | — | in-progress（2026-10-21 P3 q1542） |
| p002 | config/ci/config-rpi5.mk と platform の起動（console・timer・interrupt・memory）、image の生成 | p001 | planned |
| p003 以降 | p001 の設計で決める（SD、USB、Ethernet、framebuffer、GPU、WiFi、規約の全文） | p002 | planning |

## 現在の状態・再開点

- 2026-10-21: P3（phase-runner、Opus 5.5）が q1542 で p001 から始める。
