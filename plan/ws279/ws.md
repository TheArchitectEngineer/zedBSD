<!-- awesome-plan project=zedbsd record=ws279 -->

# WS279 Raspberry Pi 5 の GPU（V3D 7.1）と display（HVS6）

- Master: [plan/master.md](../master.md)。Status: incomplete（2026-10-21 開始）。Primary Milestone: MG008。Related: MG006。
- 出典: 2026-10-21 ユーザー「D2: 分けましょう。」「GPUは別途Mesaの初期化手順を正確にまとめてから実装しましょう。」（WS278 の D2）。担当は Opus 5.5、Fable の review は使わない（WS278 と同じ、ユーザー「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」）。

## 目的と完了の条件

- Pi 5（BCM2712、V3D 7.1、HVS6）で Keiland の graphical login が GPU で動く。
- 実装の前に、Mesa（v3dv・broadcom の compiler）と Linux の v3d・vc4 の kernel の driver の初期化と submit の手順を正確にまとめる（p001）。
- 既存の Pi 4（V3D 4.2、bcm2711）の driver との差を明らかにし、共有できる所と分ける所を決める。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | Mesa と Linux の初期化・submit の手順の調べ（V3D 7.1 の register・control list・QPU の命令の差、TFU・CSD、HVS6 の plane・scanout、firmware との分担）、既存の Pi 4 の driver との差の表、Phase の分け方 | WS278 p002 | in-progress（2026-10-21 P2 q1544） |
| p002 以降 | p001 で決める | p001 | planning |

## 現在の状態・再開点

- 2026-10-21: P2（Opus 5.5）が q1544 で p001。実装はユーザーと p001 の結果を見てから。
