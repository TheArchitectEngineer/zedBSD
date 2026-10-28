<!-- awesome-plan project=zedbsd record=ws279 -->

# WS279 Raspberry Pi 5 の GPU（V3D 7.1）と display（HVS6）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: Pi 5 で Keiland が GPU で動く: 2026-10-22 ユーザー「System Monitor を開いて、中身が出ました。」「マウスを動かして、cursor が十分滑らかです。」、2026-10-23 ユーザー「Notesは問題なかったです。」（stencil）。D1〜D4 は 2026-10-21 ユーザー「Pi5のdecisionは推しでOKです」で決定済み）
<!-- retained-after-completion --> tests/ の 5 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: MG006
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Pi 5（BCM2712、V3D 7.1、HVS6）で Keiland の graphical login が GPU で動く。実装の前に Mesa（v3dv・broadcom の compiler）と Linux の v3d・vc4 の初期化と submit の手順をまとめ（p001、[design.md](design.md)）、既存の Pi 4（V3D 4.2、bcm2711）の driver との差を明らかにして共有と分離を決める。

## ユーザーの決定

- 2026-10-21「D2: 分けましょう。」「GPUは別途Mesaの初期化手順を正確にまとめてから実装しましょう。」（WS278 の D2）
- 2026-10-21「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」
- 2026-10-21「Pi5のdecisionは推しでOKです」→ D1〜D4 は design.md §9 の推しどおり。
- 2026-10-22「System Monitor を開いて、中身が出ました。」「マウスを動かして、cursor が十分滑らかです。」、2026-10-23「Notesは問題なかったです。」（stencil）

## 結果

- p002・p003（`CONFIG_DRIVER_BCM2712_GPU`、起動時は V0 だけ、`sysctl hw.gpu.start=1` で V1〜V9）: Pi 5 D0 の実機（2026-10-22、kernel bfea9f4a2）で `v3d: V2 version 7.1 rev 10 compat 2 cores 1 QPUs 12 VPM 64 KiB PA 36 VA 36`、clock 1150000000 Hz（V3D 7.1.10・12 QPU・1.15 GHz）。noop・clear・TFU・CSD・hang と reset が全 PASS。Pi 4 と Pi 5 既定の kernel.elf は不変。
- p004（q1545）: QPU 7.1 の encoder・scheduler・register の割り当て（qpu71.c と shader71-*.c、shared の shader.c・shader-analyze.c）、host 試験（Mesa の oracle、7.1 の lane の model で木の全 shader を source と比較）PASS。
- p005: 7.1 の encoder の写し（pass・bin・shader record・texture・viewport）と Pi 4 の Vulkan の実行器で、実機の `VKQUAD ok device=zedBSD V3D 7.1 (Keiland graphics) pixels=4096`。compositor の quad（texture・blend・clear・copy・readback）が全 pixel 一致。
- p006: HVS6 の段 P0〜R3（readout・自分の dlist・PV の vblank・overlay の flip、`hvs6-flip.c` と共有の display device、`/dev/gpu0`）が実機で PASS。
- p007: `rpi5gpu.start=boot` で `login=graphical` の起動で sessiond が `DESKTOP shown`。ユーザーが System Monitor・cursor・Notes（stencil）を目視で確認。`#if HAL_BOARD_RPI5` は実行時の版と board の encoder の関数に置き換えた。

## 制限・移管

- 速さ（p008）は未着手（Master の優先順「Pi 5 の残り: ユーザーの指示の時」）。
- 規約の全文の見直しと license・類似の監査（p009）は未実施。規約は WS の外の一括の見直しへ（q1608 の扱い）。license・類似の監査を誰がいつ行うかは記録に無い（未決）。
- Keiland からの lease・cursor・zero copy の実機の行ごとの確かめと scaled overlay（p006 の残り）は未確認。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws279-p001 | Mesa と Linux の初期化・submit の手順の調べ、Pi 4 の driver との差の表、Phase の分け方 | in-progress のまま WS を閉じた（design.md 第 1 版、D1〜D4 はユーザーが「推しでOK」） |
| ws279-p002 | V3D 7.1 の骨格（電源・clock・SMS・識別・MMU・IRQ、段 V0〜V4） | in-progress のまま WS を閉じた（実機で V0〜V4 PASS） |
| ws279-p003 | job の submit（noop・clear、TFU、CSD、cache の clean、hang と reset） | in-progress のまま WS を閉じた（実機で self-test 全 PASS） |
| ws279-p004 | QPU 7.1 の encoder・scheduler・register の割り当て、固定の shader | cleared（2026-10-21、q1545、host の試験 PASS） |
| ws279-p005 | 描画の経路（shader state record・texture state・tile・Vulkan の native の部分） | in-progress のまま WS を閉じた（実機の quad 全 pixel 一致、Keiland の画面は p007 で確認） |
| ws279-p006 | HVS6 の display（引き継ぎ・plane・PV の vblank・flip・zero copy） | in-progress のまま WS を閉じた（P0〜R3 実機 PASS） |
| ws279-p007 | `drv_gpu_interface` と Keiland の graphical login | in-progress のまま WS を閉じた（desktop の session が GPU で起動、ユーザーが目視） |
| ws279-p008 | 速さ | 未着手のまま WS を閉じた |
| ws279-p009 | 規約の全文の見直しと license・類似の監査 | 未実施のまま WS を閉じた（規約は一括の見直しへ、license・類似の監査は未決） |

## 記録の所在

Phase の計画・結果（`plan/ws279/phase001`〜`phase007`）は削除する。試験（`plan/ws279/tests/`）のうち 7.1 の host 試験は plan/tools へ移す（移した先は [plan/tools.md](../tools.md)）。git の commit `fb1f6afee` の `plan/ws279/` にある。Queue の履歴は [plan/history](../history/index.md)。
