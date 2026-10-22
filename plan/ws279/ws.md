<!-- awesome-plan project=zedbsd record=ws279 -->

# WS279 Raspberry Pi 5 の GPU（V3D 7.1）と display（HVS6）

- Master: [plan/master.md](../master.md)（2026-10-21 開始）。Primary Milestone: MG008。Related: MG006。
- Status: incomplete
- 出典: 2026-10-21 ユーザー「D2: 分けましょう。」「GPUは別途Mesaの初期化手順を正確にまとめてから実装しましょう。」（WS278 の D2）。担当は Opus 5.5、Fable の review は使わない（WS278 と同じ、ユーザー「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」）。

## 目的と完了の条件

- Pi 5（BCM2712、V3D 7.1、HVS6）で Keiland の graphical login が GPU で動く。
- 実装の前に、Mesa（v3dv・broadcom の compiler）と Linux の v3d・vc4 の kernel の driver の初期化と submit の手順を正確にまとめる（p001）。
- 既存の Pi 4（V3D 4.2、bcm2711）の driver との差を明らかにし、共有できる所と分ける所を決める。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | Mesa と Linux の初期化・submit の手順の調べ（V3D 7.1 の register・control list・QPU の命令の差、TFU・CSD、HVS6 の plane・scanout、firmware との分担）、既存の Pi 4 の driver との差の表、Phase の分け方（[design.md](design.md)） | WS278 p002 | in-progress（2026-10-21 P2 q1544、設計の第 1 版、判定待ち） |
| [p002](phase002/phase.md) | V3D 7.1 の骨格（電源 PM 0x304・clock・SMS・識別・MMU・IRQ 2 本、段 V0〜V4 の印）（design §6、4〜6 LW） | p001、WS278 p002 の実機の起動 | in-progress（2026-10-22 P2、実機で V0〜V4 PASS、判定待ち） |
| [p003](phase003/phase.md) | job の submit（7.1 の最小の packet、noop・clear、TFU、CSD、cache の clean、hang と reset）（6〜8 LW） | p002 | in-progress（2026-10-22 P2、実機で self-test 全 PASS、判定待ち） |
| [p004](phase004/phase.md) | QPU 7.1 の encoder・scheduler・register の割り当て、固定の shader（10〜14 LW、host だけで並べられる） | p001 | cleared（2026-10-21 Q1、q1545、host の試験 PASS、実機は p005） |
| p005 | 描画の経路（shader state record の D0 の形、texture state、tile の大きさ、Vulkan の native の部分）（8〜10 LW） | p003・p004 | planning |
| [p006](phase006/phase.md) | HVS6 の display（引き継ぎ、UPM、plane、PV の vblank、flip、zero copy）（8〜10 LW） | p002 | in-progress（2026-10-22 P2、P0〜R3 実機 PASS、`/dev/gpu0` の display device、client からの確認と scaled overlay が残り） |
| p007 | `drv_gpu_interface` と Keiland の graphical login（4〜6 LW） | p005・p006、WS278 p004 | planning |
| p008 | 速さ（4〜6 LW） | p007 | planning |
| p009 | 規約の全文の見直しと license・類似の監査（3〜4 LW） | p002〜p008 | planning |

## 現在の状態・再開点

- 2026-10-21: P2（Opus 5.5）が q1544 で p001。実装はユーザーと p001 の結果を見てから。
- 2026-10-21 P2: p001 の設計の第 1 版（[design.md](design.md)）。p002〜p009 は案（見込み 47〜64 LW）。再開点: ユーザーの判断 D1〜D4（design §9）と WS278 p002 の実機の起動の結果。p004（compiler）は host だけで先に始められる。

- 2026-10-21 ユーザー「Pi5のdecisionは推しでOKです」→ D1〜D4 は design.md §9 の推しどおり。p004（QPU 7.1、host だけ）を P2 q1545 で始める。p002・p003・p005〜は WS278 の起動の実機の確認の後。
- 2026-10-21 P2 q1545: p004 の 7.1 の compiler（qpu71.c と shader71-*.c、shared の shader.c・shader-analyze.c）と host 試験（Mesa の oracle、7.1 の lane の model で木の全 shader を source と比較）が PASS。再開点: Q1 の判定、p005 で使う。
- 2026-10-22 P2: p002・p003 を実装（`CONFIG_DRIVER_BCM2712_GPU`、起動時は V0 だけ、`sysctl hw.gpu.start=1` で V1〜V9）。Pi 5 D0 の実機で V3D 7.1.10・12 QPU・1.15 GHz、noop・clear・TFU・CSD・hang と reset が全 PASS（dmesg）。Pi 4 と Pi 5 既定の kernel.elf は不変。再開点: Q1 の判定、次は p005・p006。
- 2026-10-22 P2: p006 の段 P0〜R2（readout・自分の dlist・PV の vblank・overlay の flip）が D0 の実機で PASS。残りは display device（lease・3 slot・cursor・zero copy）と p005。
- 2026-10-22 P2: p006 R3（HVS6 の flip の写し `hvs6-flip.c` と共有の display device）が実機で PASS、`/dev/gpu0`。次は p005（描画の経路）。
