<!-- awesome-plan project=zedbsd record=ws283-p001 -->

# ws283-p001 bcm2711 の Vulkan の compute の調べと設計

- 親: [ws283](../ws.md)。担当: P2（2026-10-23、Q1 の依頼、worktree `agent/p2-compute`）。Queue: Q1 が記録する。
- Status: in-progress（設計の第 1 版、Q1 の判定とユーザーの判断 D1〜D4 待ち）
- 範囲: 調べと設計だけ。実装・build・実機の試験はしない。

## 目的と受け入れの条件

1. compiler（SPIR-V → V3D 4.2・7.1 の QPU）が compute に欠く物を、今の code と Mesa v3dv の compute の扱いから洗い出す（workgroup・invocation の ID、共有 memory、barrier、storage buffer の読み書き、vector の load・store、atomic、push constant、specialization constant、loop）。
2. CSD の dispatch の設定（register の queue、workgroup の大きさの限界、共有 memory の大きさ、supergroup）を 4.2 と 7.1 で、Mesa の出典付きでまとめる。
3. 実行器の部品（compute の queue family、pipeline、storage の descriptor、`vkCmdDispatch` → CSD の job、render の job との同期、indirect）を決める。
4. 試験の計画（host と Pi 5 の実機）と、YOLOX-Nano 416×416 の性能の見込み。
5. Phase の分け方と LW の見込み、ユーザーの判断が要る点。

## 結果（2026-10-23 P2）

- [design.md](../design.md) の第 1 版。要点:
  - SPIR-V の前段（共有）は compute を既に持つ。欠けは bcm2711 の後段と実行器。後段には分岐が全く無く（loop が通らない）、整数の乗算・比較・除算、TMU の一般の access も無い（design §1.1 の表）。scheduler・register の割り当ては 1 block だけ。
  - kernel の CSD の job は 4.2・7.1 ともにあり、7.1 は実機の self-test で動いた。userland の `libvulkan`・wire の opcode・`vkcompute` の試験は既にある。
  - 見込み: p002〜p010 で 51〜77 LW。YOLOX-Nano 416 fp32 は Pi 5 で最初 3〜8 fps、詰めて 10〜20 fps（推定、根拠は design §3.3）。
- 調べに使った物: Z の `src/drivers/gpu/bcm2711/{compiler,vk}/`・`v3d-job.c`・`v3d71-job.c`・`v3d71-diagnostic.c`・`v3d71-cl.c`、`src/drivers/gpu/compiler/{ir.h,spirv-compute.inc}`、`include/uapi/gpu-op.h`、`userland/tests/vkcompute/`、WS279 の design §2.5・§3.4。Mesa 25.3.6 の `vulkan/v3dv_cmd_buffer.c`（4221〜4490 行）、`common/v3d_csd.h`・`v3d_util.c`、`compiler/nir_to_vir.c`（43〜430・3357〜3790・4775〜4832 行）、`compiler/vir.c`（1996〜2150 行）、`qpu/qpu_instr.h`（106〜127 行）、`vulkan/v3dv_device.c`（958〜961 行）。
- 実行したコマンド: 読むだけ（`grep`・`sed -n`・`ls`・`wc`）。build・試験は無し。

## 判断が要る点

design §5: D1 Pi 4 の実機を完了の条件から外すか、D2 fps の数値の目標を置くか、D3 fp16 は保存だけを p008 で、D4 atomic・storage image・subgroup を最初の範囲から外すか。Q1: D5 lane の model（WS141・WS279 の試験の file）の置き場所。

## 未実施・残り

- design-reviewer の review（AGENTS の「設計ごとに自動で必ず」の対象なら Q1 が起動する）。
- 確かめていない事（design に「推定」「実機で確かめる」と書いた物）: 7.1 の slice をまたぐ共有 memory の可視性、2 thread の compute の CFG5、TMU の実効の帯域、dispatch の 1 回の費用、specialization constant の前段の扱い。

## 再開の条件

Q1 の判定とユーザーの D1〜D4 の回答。その後 p002（encoder）から。
