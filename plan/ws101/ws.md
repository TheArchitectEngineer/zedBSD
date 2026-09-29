<!-- awesome-plan project=zedbsd record=ws101 -->

# WS101: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化が 5330 の GPU で動く）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

「デモのときに来客から、Compute Shaderが使えるのか聞かれて、もし使えないと答えたらがっかりさせてしまいますね。これはOSCデモ（10月17日）には
使えるようにしたいです。ただし、優先事項の中では最優先ではない、くらいです。NoctにはOpenGL ESを使ってCompute Shaderを生成してGPU
アクセラレーションを行う自動並列化機能が入っており、これが動くようにするのをゴールにしたらどうでしょう。Compute Shaderに対応しておけば、
ローカルLLMも動かせるようになって、夢が広がります。」

期限は 2026-10-17 の OSC のデモ（新規実装の期間は 10/10 ごろまで。master の判断）。優先は fg010 の中で最優先ではない。

## 達成基準（案、2026-09-30 main）

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| G1 | i915 のネイティブの実行器で Vulkan の compute が動く: `vkCreateComputePipelines`、`vkCmdDispatch`・`vkCmdDispatchIndirect`、local size、組み込みの ID、shared memory と `barrier()`、SSBO と shared memory の atomic | compute の試験の群（足し算・reduction・shared memory と barrier・atomic・indirect）が 5330 の実機（passthrough と素の機械）で結果が正しい |
| G2 | libglesv2（WS068）の OpenGL ES 3.1 の compute が動く: GLSL ES 3.10 の compute shader、`glDispatchCompute`、SSBO（`glBindBufferBase`）、`glMapBufferRange`、`glMemoryBarrier`、headless の EGL（pbuffer） | GLES の compute の試験が Venus と i915 で結果が正しい |
| G3 | Noct の自動並列化（`accel_opengles.c`、headless の EGL と GLES 3.1）が zedBSD で GPU の backend を選び、見本の program（例: 大きな配列の演算、行列の積）の結果が CPU と一致し、大きさ N で CPU より速い（N と倍率は p001 で決める） | 5330 の実機で時間を比べる |
| G4 | fg010 の台本の場面として見せられる（Noct の見本を GPU と CPU で走らせて時間を比べる） | 実機でのユーザーの確認 |

subgroup の操作（`subgroupAdd` 等）、image load/store、atomic counter は、Noct と試験が要らなければ範囲の外。ローカルの LLM は Future Work（F-061）。

## 分かっていること（2026-09-30 main の調べ）

- **i915**: device の情報は queue に `VK_QUEUE_COMPUTE_BIT` を立て、compute の上限値も報告するが、実行器（`src/drivers/gpu/i915/render/command.c`）は
  `vkCmdDispatch`・`vkCmdDispatchIndirect`（opcode 110・111）を扱わず ENOTSUP で拒む。compiler（`compiler/spirv.c`）は GLCompute を扱わない。
  kernel の試験（`tests/execution/eu-test.c`）は GPGPU_WALKER で compute のスレッドを実機で走らせている（Alder Lake は Gen12 の GPGPU_WALKER の世代）。
- **libglesv2**: GLES 3.1 の compute は ws068-p035（planning・保留）。
- **Noct**: `userland/base/noct/noct/src/accel/accel_opengles.c` は「Headless OpenGL ES 3.1 accelerator backend using EGL」で、使う API は
  EGL（`eglGetPlatformDisplayEXT`・`eglCreatePbufferSurface`・`eglBindAPI` ほか）と GLES（`glDispatchCompute`・`glBindBufferBase`・
  `glMapBufferRange`・`glMemoryBarrier` ほか）、workgroup の大きさ 64。**guest の Noct は `NOCT_ENABLE_ACCEL` が OFF で build されている**。
  有効にするのは Noct の build の規則の変更で、toolchain にあたるので **main の許可が要る**（AGENTS.md）。

## 危険

- compute に特有の設定（SLM の大きさ、barrier の数、スレッドの数）を誤ると GPU が固まり、切り分けに時間がかかる（WS031 の EU のハングの前例）。
- Venus の経路（QEMU）は host の driver で compute が動く見込みだが、Kei で compute を使った試験の記録は無い。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws101-p001 | 設計: compiler（GLCompute・ID・SLM・barrier・atomic）、実行器（PIPELINE_SELECT GPGPU・MEDIA_VFE_STATE・interface descriptor・CURBE・GPGPU_WALKER・indirect）、GLES 3.1 の compute、Noct の accel を有効にする build の変更の案、試験と G3 の見本と N・倍率 | planning | — |
