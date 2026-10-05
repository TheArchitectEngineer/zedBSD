<!-- awesome-plan project=zedbsd record=ws038 -->

# WS038: Intel Arc dGPU対応

<!-- awesome-plan-current:start -->
Status: planning（要検討・ブロック、2026-10-05 ユーザー「要検討状態にしてブロックする」）。以前: planning
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 番号の予約のみ（Intel Arc dGPU）
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 単一目標

Intel Arcの単体GPU（dGPU）のドライバを実装する。

## 担当

別契約のエージェントが担当する見込み（2026-09-23ユーザー指示）。このファイルは番号の予約として作った。範囲・受け入れ条件・Phaseは、担当するエージェントが計画する。

## 前提（参考）

- GPUの共通層は `src/drivers/gpu/`（drv_gpu）、既存のbackendはVenus（WS014）とi915（WS029・WS031）。
- 標準Vulkan libraryはWS030（`userland/desktop/libvulkan/`）。
- 規約とGuardrailは `plan/guardrail.md`、運用は `plan/master.md`「実行体制とQueue運用方針」。
  デバイスドライバには設計Phaseを入れる。HALの変更は差分ごとの事前承認が要る。

## Phase一覧

未作成。

## 要検討・ブロック（2026-10-05）

ユーザーの指示で要検討の状態にしてブロックする。ユーザーと方針を決めるまで Queue に入れない。
