<!-- awesome-plan project=zedbsd record=ws039-p013 -->

# ws039-p013: 段 A5c 取り消しと復帰（終わらない仕事の取り消し、queue の reset）

Status: cleared（2026-10-16 Q1 が判定、merge 7e8aa264a。2026-10-16 P3: GFX の queue の reset、compute 専用の family と compute の queue の reset を実機で ok。merge と判定は Q1）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1234 / q1234-i01（p013 → p014 → p015 → p016、各段は実機で ok の後に main）
承認: 2026-10-16 ユーザー「p013,p014,p015,p016を現在のサブエージェントで継続してください。」
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`）

## 範囲（[design.md](../design.md) §16 の p013 の行、§11）

run の期限、session の状態機械（11.3）、GFX の queue の reset（MQD の写しからの戻し、preamble の出し直し）、compute の queue の reset、record の backup と流し直し、`isolate` と quarantine、compute 専用の family の最小の経路、`vkhang`、host 試験 (k)。**実機: vkhang の app だけ DEVICE_LOST、reset の後 desktop 継続、vkquad ok、compute の queue の reset**。

## 実装（2026-10-16 P3）

- `cp.c`: `amdrdna_cp_graphics_reset(device, context)`。KIQ（scheduler の queue）に CP_VMID_RESET（0x1e53）= context の bit と pipe 0 の queue の bit を書き、MQD の active の語（dword 130）が 0 になるのと CP_VMID_RESET が 0 になるのを待つ → MQD を init の時の写し（`image_copy`）から戻し、ring を filler で埋めて read/write pointer を 0 → doorbell の範囲を書き直し → KIQ の MAP_QUEUES → ring test → preamble（clear state と partition、`graphics_preamble` に切り出し）を出し直す。出典 Linux v6.19 gfx_v10_0.c の reset_kgq・kgq_init_queue・amdgpu_ring.c の reset_helper_end。`command-format.c` に register の待ちの packet（`amdrdna_pm4_wait_register`）。
- `space.c`: graphics の run が期限（`VK_NODE_RUN_MS` 2000）で終わらない時、space を喪失（session に EIO、既存）にした後に queue を reset し、時間を印に出す。reset が失敗したら device の fault（`node.c` の `amdrdna_node_device_failed` → `drv_gpu_report_error`）。executor の run は同期の 1 worker なので ring には詰まった run の後ろに他の仕事が無く、backup と流し直しは要らない（他の session の run は reset の後に普通に積まれる）。
- 試験の app `plan/ws039/tests/vkhang/`（`main.c`、pixel program `hang.frag`（3x+1 を 0 から、終わらない）・`loops.frag`（27 から 111 歩、対照））、`vkhang-build.sh BUILD`（glslc で SPIR-V の header を作って image の libvulkan に link）。出力 `VKHANG lost after=<ms> result=<VkResult>` / `VKHANG ok after=<ms> pixel=…`。

## 実機（2026-10-16 20:03〜、10.0.10.3、p017 の 2 段目の上）

- `vkhang --loops`（対照）: `VKHANG ok after=37 pixel=ff6f0000`（111 歩の 255 分の色）。
- `vkhang`: `VKHANG lost after=2567 result=-4`（VK_ERROR_DEVICE_LOST、libvulkan の `VULKAN kernel error errno=5 device lost`）。kernel: `space 26 lost: its run did not end (42)`、`A5 kiq ring test ok`×2、`A5 gfx ring test ok`、**`graphics queue reset for context 10 in 774ms, the queue runs again`**。その後 `amdrdnactl display` の presents が進む（12 → 13）、vkblend 20 case ok、`vkhang --loops` ok、`fault=0 hung=0`。
- 続けて vkhang を 3 回（各 `lost after=2546〜2547`、reset 各 774 ms、context 13・14・15）→ aat で App Home の開閉 6 回と app の起動（Files が開く）が通る。capture `build/tmp/amdrdna-hw-p9-7/s16.png`（P3 の worktree、desktop が描けている）。
- reset の 774 ms は毎回同じ: 中の待ち（ring test の poll・scheduler の fence）の刻みと見る。短くするのは後（体感は期限の 2 s ＋ 0.8 s の間 desktop が止まる）。

## 追加（2026-10-16 20:10〜 P3、(2)〜(5)）

- (2) CP の例外（priv reg・priv inst・bad opcode の割り込み）: `engines.c` が IV の context で `amdrdna_space_command_error` を呼び、その context の持ち主の space を喪失にして session に EIO（その run は期限で reset へ）。実機で起こす手段が無く**未確認**。
- (3) `isolate`（`runtime.c` の `runtime_isolate`）: space が喪失でかつ GPU の上に何も残らない（queue の reset が済んだ）時だけ 0、session に isolated の印（以後の仕事を拒み、blob の destroy で VRAM を返さない）。それ以外は EIO（共通の層が device の fault へ）。
- (4) host 試験: `amdrdna-engines-test.c` に register の待ちの packet（Linux の emit_reg_wait の形）と SDMA の fill の packet。PASS の行 440。session の状態機械の host 試験（design の (k) の backup と re-emit）は、backup の要らない形（同期の 1 worker）にしたので対象が無い。
- (5) reset の時間: 各段の確かめを scratch の register の ring test（1 回 約 257 ms かかっていた）から fence の待ちに替えた。実機 20:23（kernel 1b7a5279）: `graphics queue reset steps: reset 2ms, attach 1ms, test 1ms, preamble 1ms`、全体の印は 272〜319 ms（差は印の行を console に出す時間の見込み）。vkhang 3 回とも `lost after=2570` 前後、その後 vkblend 20 case ok・`vkhang --loops` ok・`fault=0 hung=0`。

## 追加（2026-10-16 20:30〜 P3、(1) compute 専用の family）

- executor（Q1 の許可の範囲: `vk-execute.c`・`vk-state.c` は compute の道の関数・分岐の追加だけ、graphics の道は変えない）:
  - `vk-device.c`: queue family を 2 つに（0 = graphics|compute|transfer、1 = compute だけ、各 1 queue）。`vkGetDeviceQueue2` と `vk-command.c` の pool は family 0・1 を受ける。
  - `vk-pipeline.c`: `vkCreateComputePipelines`（`VkComputePipelineCreateInfo` を読み、compute の module を `DRV_GPU_STAGE_COMPUTE` で compile して `kernel` に。graphics の create と同じ枠を `pipeline_create(..., kernel)` で共有）。`pipeline_bindings` は kernel の workgroup 数の system set（`DRV_GPU_IR_SYSTEM_SET`）を通す。
  - `vk-record.c`: bind pipeline・bind descriptor sets の compute の bind point（bind point を record の value 0 に。pipeline の種類が bind point と違えば command buffer を失敗）、`vkCmdDispatch`。
  - `vk-execute.c`: compute の bind point の pipeline と set を別に保つ（`kernel_pipeline`・`kernel_sets`）、`execute_dispatch`（workgroup 数 3 語を table の領域に置き、kernel の table を `execute_kernel_resources` で書き、`amdrdna_vk_state_kernel`＋DISPATCH、その後 barrier）。family 1 の command buffer は compute の queue（`amdrdna_vk_gpu_run_kernel` → `AMDRDNA_TRACKED_COMPUTE` = compute の queue 0）で走り、buffer の始めと barrier は compute の形、描画と転送の record は ENOTSUP。universal の family の dispatch は GFX の ring のまま（design 7.4）。
  - `vk-state.c`: `amdrdna_vk_state_start_kernel`（compute の preamble と cache の acquire だけ）、`amdrdna_vk_state_barrier_kernel`（CS の drain、RELEASE_MEM、待ち、acquire。PFP の sync・PS の drain・CB の meta は無し）。
- kernel: `cp.c` の `amdrdna_cp_compute_reset(device, index, context)`: **context の wave を SQ_CMD の KILL（broadcast、VMID 指定）で殺す** → KIQ に UNMAP_QUEUES（RESET）→ queue を選んで HQD_ACTIVE が 0 を待つ → MQD を写しから戻し ring を空に → MAP_QUEUES → fence。`space.c` の期限切れは ring の種類で graphics か compute の reset を選ぶ。出典 Linux v6.19 gfx_v10_0.c の reset_kcq・kcq_init_queue、amdgpu_amdkfd_gfx_v10.c の wave_control_execute、gc_10_1_0 の SQ_CMD と navi10_enum.h。
- 実機 1 回目（20:53、kill 無し、cfd5d7801）: `vkhang --compute --loops` = `VKHANG ok after=36 steps=111`（compute の family の dispatch が正しく走る）。`vkhang --compute` は期限で lost になったが **KIQ が UNMAP_QUEUES RESET を 1 s で終えず**（`the scheduler did not run the compute reset (42)`）、device の fault（以後の仕事なし）。無限 loop の wave が居ると firmware の RESET の dequeue が終わらない。
- 実機 2 回目（20:58、wave の kill を足した kernel）: `vkhang --compute` を 3 回 → 各 `VKHANG lost after=2533〜2539 result=-4`、kernel `compute queue 0 reset steps: reset 1〜2ms, attach 1ms, test 1ms`、`compute queue reset for context 8/13/14 in 253〜256ms, the queue runs again`。各 lost の後 `vkhang --compute --loops` ok（steps=111）、`vkhang --loops` ok（pixel=ff6f0000）、vkblend 20 case ok、graphics の `vkhang` は従来どおり graphics の reset（271 ms）で lost、その後も compute・graphics とも ok。`amdrdnactl display` の presents 0 → 10（desktop は描き続けた）、`fault=0 hung=0`。証拠 `build/tmp/p13-compute2/{run.txt,dmesg.txt}`（P3 の worktree）。
- 副次: 起動の A5 の vm fault の検査（compute の queue 2 で意図して fault させる）の後、その run が終わらず queue 2 が詰まったままだったのが、今は起動時に `compute queue 2 reset ... in 259ms` で戻る（以前は queue 2 を誰も使わないので見えていなかった）。
- reset の印の 250 ms 余りは、各段の計時（計 3〜4 ms）の外: kmsg の 1 行ごとに kernel の text console が 1920x1080 を描き直す時間と見る（P5 が p019 で同じ事を見つけた、present_claim で text console を suspend していない）。
- host 試験: `amdrdna-vk-host-test.sh` に compute の family の試験（family の報告、ids.comp の compute の pipeline、family 1 の pool の command buffer で bind と 2x1x1 の dispatch、compute の queue だけで 1 run・graphics の packet なし、interpreter で各 workgroup を再生して 48 invocation の id と group 数 2/1/1 を照合、3 番目の family の pool と bind point の違う pipeline の拒否）。PASS 193。`amdrdna-host-test.sh` PASS。build（`plan/ws039/tests/config.mk`）warning 0。audit-vk の FAIL 78 は全て既存の file の既存の語（今回の追加の語は 0）。
- vkhang: `--compute`（`vkhang/walk.comp`: buffer の 1 語目から 3x+1、0 なら終わらない、27 なら 111 歩）。

## 残り（この版の外）

- 無し（受け入れの実機の項目は済）。compute の family の転送（copy・clear）は compute の queue では拒否（最小の経路、design 7.4）。
