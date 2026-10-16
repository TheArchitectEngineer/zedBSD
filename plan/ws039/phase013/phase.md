<!-- awesome-plan project=zedbsd record=ws039-p013 -->

# ws039-p013: 段 A5c 取り消しと復帰（終わらない仕事の取り消し、queue の reset）

Status: in-progress（2026-10-16 P3: GFX の queue の reset を実機で ok。compute の family・compute の reset・isolate は残り）
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

## 残り（この版の外、順に）

- compute 専用の queue family と compute の queue の reset（UNMAP_QUEUES RESET）、vkhang の compute の場合。
- CP の例外の割り込み（priv reg・priv inst・bad opcode）での喪失、VM fault の run の期限。
- `isolate` と quarantine（今は isolate が無く、stop の期限を越えると device の fault）。
- host 試験 (k)。
