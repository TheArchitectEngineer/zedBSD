<!-- awesome-plan project=zedbsd record=ws291-p005 -->

# ws291-p005 RDNA の NGG A: passthrough の primitive shader（正しさの土台）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-27 05:00 P2、q1611-i01。2026-10-27 13:49 受け入れを全部確認、Q1 の判定待ち）。
- 出典: 2026-10-27 04:44 ユーザー（Q1 の問い「NGG の A〜C をどう進めますか？」へのクリック）「A〜C を今進める（推し）」。
- 依存: p004（cleared）。

## 範囲

1. vertex program を NGG の passthrough の primitive shader として compile する（GS_ALLOC_REQ、primitive の export、position・parameter の export）。
2. executor が NGG の register（GS の program、subgroup の大きさ、GE_CNTL、VGT_SHADER_STAGES_EN など）を書き、legacy と NGG の切り替えで VGT_FLUSH を出す。
3. 切り替えの option: boot の行 `amdrdna.vk.ngg`（0 legacy、1 NGG）。既定は確かめが済むまで legacy。
4. 受け入れ: mview の 1 体で Linux と pixel 差 0、6500 体で 8 pixel から悪化しない、desktop の keiland-shot が正常、`measure.sh hang` で回復、GPU の ms を記録。

## 記録

- 2026-10-27 05:00 P2: 着手。RADV 25.0.7 の NGG（radv_shader_args.c の merged GS の引数、radv_emit_hw_ngg、gfx10_get_ngg_info、ac_nir_lower_ngg の passthrough、ACO の select_shader）を読んだ。
- Q1 の了解（2026-10-27）: plan/ws039/tests の GFX10 の interpreter（gfx10-machine.c・.h、gfx10-graphics-test.c）をその場で広げてよい。

### 実装（commit、worktree p2-ws291）

| commit | 内容 |
| --- | --- |
| 71179a521 | compiler の primitive shader（s2・s3 の count、GS_ALLOC_REQ、prim の export、v5・v8 の入力、user SGPR s8〜）、program の GS の register、executor の NGG の register、VGT_FLUSH、`amdrdna.vk.ngg`、interpreter の NGG（s_bfe・s_bfm・s_sendmsg・prim export） |
| 13be7d3e5 | GE_PC_ALLOC を書かない、wave の count を 7 bit、ESGS の accumulator を 0、hang の時に GC の status を mark |
| 42755dd31 | Navi1x の s_barrier（GS_ALLOC_REQ の前）、既定を legacy に |
| 4ce3b01b7 | GC の status を vk timing の度にも mark |
| 85dc855e5 | RADV の subgroup（128 頂点・128 primitive、4 wave）、wave 0 だけが alloc |
| 87b94e85d | primitive の無い wave・頂点の無い wave は export を飛ばす（execz） |
| 63080fe62 | GE_PC_ALLOC（oversubscription なし）と LATE_ALLOC_VS 0 を IB の始めに |
| 9b32ffc49 | VGT_SHADER_STAGES_EN の VS_W32_EN |
| 21501f322 | RSRC1_GS の WGP_MODE、VGT_GS_INSTANCE_CNT の CNT 1 |
| d53eeb365 | 内部の quad の vertex program も NGG、legacy と NGG のどちら向きの変化でも VGT_FLUSH |
| 19da50679 | VGT_ESGS_RING_ITEMSIZE 1、PA_SU_HARDWARE_SCREEN_OFFSET 0 |
| 030ab6a38 | s_setprio 3 |
| eacf8bf3b | hang の時に SQ の wave を走査して mark（Linux v6.12 gfx_v10_0 の wave_read_ind の手順） |
| 4d2b9866a・ece59a877・12013084d・28ee07087・db9b2efd2 | 助言役（Fable）の一覧の反映: RSRC3/4 を SET_SH_REG_INDEX（index 3）、wave の走査を 32 instance、GE_PC_ALLOC・late alloc・CU mask を RADV の live 値、GE_MULTI_PRIM_IB_RESET_EN 0、PA_SU_SC_MODE_CNTL の PTYPE、hang の時の status の範囲 |
| c92076516 | 試験 plan/ws291/tests/nggshape（形を選んだ 1 draw、NGGSHAPE ok/lost の 1 行）と nggshape-build.sh |

host 試験: `plan/ws039/tests/amdrdna-compiler-host-test.sh` PASS（vertex の module ごとに +ngg の variant: alloc の要求、prim の export、wave 番号、頂点の無い wave）、`amdrdna-vk-host-test.sh` PASS。amd64 の vmunix は warning 0（build/ws291/p005-vmunix.log）。

### 実機（10.0.10.3、RX 5600 XT、zedBSD USB）

- 1 回目（71179a521）: 最初の draw で GPU が止まり、queue の reset も失敗。
- 以後、NGG の desktop は描ける（lock 画面・desktop の描画を確認）が、起動直後から GRBM_STATUS に SX_BUSY・CP_BUSY が残り（0xa0103028）、mview の最初の frame で GPU が止まる（GE・SPI・SX・PA busy、0xa2710028）。
- hang の時の wave の走査: NGG では `waves valid 0`（shader の wave は残っていない）。走査の正しさは legacy の vkhang で確かめた（`waves valid 72`、PC・命令・status が出る、queue の reset 4 ms で回復、desktop も戻る。build/ws291/measure/p005/hang-legacy）。→ 止まっているのは shader ではなく、export の領域（position・primitive・parameter）が返らない側。
- RADV（Linux、`RADV_DEBUG=vs,asm,nocache,nonggc RADV_PERFTEST=gewave32`）の NGG の vertex program を同じ機械で dump した（build/ws291/measure/p005/linux-dump2/mview.log）: zedBSD の program と構造は同じ（setprio、s_barrier、wave 0 の alloc、execz を挟んだ prim の export、頂点の部分）。RADV は mview を最後まで描く。register の差が残っている見込み。
- legacy（`amdrdna.vk.ngg=0`）の起動では ue0（USB の LAN）の link が上がる。link が上がらなかったのは GPU が起動直後に止まった起動だけ。
- 別件: compositor の起動の各段が 1.5 s 前後ずつ止まる事が、legacy でも base の kernel（368bbfd99 の source）でも warm な再起動の後に出る（cold の起動では出ない）。この Phase の変更が原因ではない。

### 切り分け（2026-10-27 09:00〜、診断用の kernel は commit しない: build/ws291/measure/p005/diag7〜9.patch）

- Linux（RADV の NGG の mview の後）の idle の GRBM_STATUS は 0x00003028 で clean。GC の config register 229 個は Linux と同じ。
- 診断の mode 3（内部の quad は legacy、app の pipeline だけ NGG）でも mview は hang。
- SX_BUSY の残留（GRBM_STATUS 0xa0103028、CPF_BUSY も）は、最初の NGG の draw（compositor の 6 頂点の quad 24 個の run）の直後に立つ（run の完了の度に読む診断で確定）。legacy では立たない。それでも desktop・nggshape は描ける。
- nggshape は全部通る: 5 万 triangle・indexed・depth・mview の VS・16 draw、index の再利用を悪くした並び（--shuffle）、全部 pixel に掛からない tiny な triangle（--tiny）、全部 cull される draw（--cull --allback）、その後の通常の draw（--tail）。→ 全部 culled の subgroup・局所性の悪い index は原因でない。
- mview の model（qs40）を削って切り分け（material は全部 opaque・cull none にしても hang）: 小さい test model は 60 frame 通る。mesh 別に laukku は通り、Face（4043 頂点・7076 triangle）は hang、texture を外した Face も hang → geometry に依る。Face の triangle の範囲で二分している。

### 最小の再現（2026-10-27 10:00〜13:30、診断の kernel は commit しない: build/ws291/measure/p005/diag10〜13.patch、diag13-full.patch が最後の全部）

- mview の Face の mesh を nggshape に読ませる（`--mesh=FILE`）と、nggshape だけで GPU が止まる。texture・depth・pixel（scissor で全部外す `--corner`）に依らない。Face の 27 triangle → 15 → 頂点 10・primitive 13（三角 0〜8 と三角 0 の重複 4 個）まで絞れた。
- もっと単純な再現: **`nggshape --indexed --triangles=16 --repeat=1000`**（3×3 の格子の 8 cell、頂点 15・primitive 16 の 1 subgroup）。zedBSD の NGG では 4〜5 回目の submit で止まる。1 回だけなら止まらないが、**最後の三角形（primitive 15 = 頂点 10・14・13、新しい頂点 14 を最後に持ち込む三角形）が描かれない**（drawn 54656、正しくは 58311。`--map` で確認）。legacy（ngg=0）と Linux RADV（`RADV_PERFTEST=gewave32 RADV_DEBUG=nonggc`、pswave32 でも）は 58311 で、1000 回でも止まらない。→ 失われた primitive の export 領域が返らず（SX_BUSY の残留）、溜まって止まる、と読める。
- 並びの依存: 同じ 16 三角形で、primitive 15 を先頭に回すと全部描かれる。先頭を末尾に回しても全部描かれる。`--draws=2`（8 個ずつ）も全部描かれる。`--probe`（probe.vert、頂点ごとの実行回数を storage buffer に数える）では頂点 0〜14 が各 1 回走っている（頂点の lane は全部走る）。→ 失われるのは primitive の側（最後の primitive の export）の見込み。
- 否定した仮説（nggshape で全部通る）: 全部 culled の subgroup（`--cull --allback`）、pixel に掛からない tiny な三角形（`--tiny`）、重なる小さな三角形（`--cluster`）、面積 0（`--degenerate`）、再利用の悪い index（`--shuffle`）、texture の sampling（`--texture`、mview の pixel program）、mview の変換（`--push`）、繰り返し（`--repeat`）、compositor との並行、legacy→NGG の切り替え（mode 1 でも同じ）、clear の draw（`--keep` でも同じ）。
- register の変種（診断 kernel、boot 行 `amdrdna.vk.nggx`）: bit 0（CU mask 全部・late alloc 0）+ bit 1（GE_PC_ALLOC を oversubscription なし 0x7fe）→ 変わらず hang。
- RADV との IB の突き合わせ: LD_PRELOAD の plan/ws291/tests/linux/ibdump.c で RADV の IB（preamble を含む）を取り、plan/ws291/tests/linux/pm4-decode.py で register 名に直した（build/ws291/measure/p005/radv-ib/ib16b.dec）。我々の IB は診断 kernel で klog に出した（radv-ib/ours16.dec）。NGG の register（VGT_GS_ONCHIP_CNTL 0x2004007e、GE_CNTL 0xf280、VGT_SHADER_STAGES_EN 0x02c12010、GE_MAX_OUTPUT・GE_NGG_SUBGRP_CNTL・IDX/POS_FORMAT・RSRC2/3/4_GS・GE_PC_ALLOC・PA_SC_NGG_MODE_CNTL、preamble）は一致。違うのは PA_CL_GB_*_CLIP_ADJ（RADV 255、我々 1.0）、PA_SC_MODE_CNTL_0（RADV 0x22 = ALTERNATE_RBS_PER_TILE）、PA_SU_PRIM_FILTER_CNTL（RADV 0xc0000000）、PA_SC_BINNER_CNTL_0（RADV は binning を使う）、RSRC1_GS の VGPR 数・FLOAT_MODE、PS の wave64、RADV が primitive restart の書き込みの前に出す SQ_NON_EVENT（has_prim_restart_sync_bug）。shader（ACO の dump と我々の encoding）は機能的に同じ（ACO は exp prim の未使用の VSRC を 0x80、我々は v0）。

### 機械の状態（2026-10-27 13:30）

- 10.0.10.3 は zedBSD（USB）で起動中で GPU が止まっている（diag12 の nggx=0x100 の試行の結果。0x100・0x200 の変種は多 wave の subgroup に正しくない設計で、desktop が即座に止まった。結果は判定に使わない）。
- stick の /boot/boot0/vmunix は vmunix-diag12（診断）、zedbsd.cfg は `amdrdna.vk.ngg=3`・`amdrdna.vk.nggx=256`。次の世代は Debian を mount して（/dev/sda2）通常の kernel と `amdrdna.vk.ngg=0`、nggx の行を外すこと。
- 入れ直し（SwitchBot）で Debian に戻る。機械の操作は host の /tmp/zedbsd-desktop.lock を flock で取る（P4 と共用）。

### 原因と修正（2026-10-27 13:35〜13:49、P2 第 6 世代、Fable 5.1 High の助言 2: build/review/ws291-ngg-advice-fable-2.md）

- 原因: NGG の prologue の `exp prim … done` の直後に exec を頂点の lane に書き換えており、間に `s_waitcnt expcnt(0)` が無かった。GFX10 の export は EXEC を発行時でなく許可された時に読む（Mesa 25.0.7 aco_insert_waitcnt.cpp の gen の Format::EXP、LLVM SIInsertWaitcnts.cpp）。wave の primitive 数 > 頂点数の時（頂点 15・primitive 16）、最後の primitive の export が落ち、その export 領域が返らず（SX_BUSY の残留）、溜まって止まる。
- 修正（一般の形、助言の (a)）: commit 6beeeab3e。`gfx10-wait.c` の note_issued で、export の読みのうち VGPR に加えて EXEC_LO も export counter に hold する → exec を書く命令の前に place_waits が `s_waitcnt expcnt(0)` を自動で置く。模型 `plan/ws039/tests/gfx10-machine.c`: export が EXEC を pending にし、その間の exec の書き込み（s_cselect・s_mov・s_and_saveexec）を「an exec write before the export counter」で fail。
- host: 修正前（模型だけ新しい）は +ngg の 6 variant（basic・query・shapes・sparse・textured・quad）が上の理由で FAIL、修正後 `amdrdna-compiler-host-test.sh` PASS（52、build/tmp/p005-hosttest-after.txt）、`amdrdna-vk-host-test.sh` 197 checks 0 failures。disassembly: `exp prim v0 … done` → `s_bfe/s_bfm/s_cmp` → `s_waitcnt expcnt(0)` → `s_cselect_b32 exec_lo`。+ngg の期待の語は変更不要だった。amd64 vmunix warning 0（build/ws291/p005-fix-vmunix.log、sha256 先頭 f86cad802a3ab423）。

### 受け入れ（実機 10.0.10.3、RX 5600 XT、2026-10-27 13:42〜13:49、kernel f86cad802a3ab423、boot 行 `amdrdna.vk.ngg=1`・`amdrdna.vk.timing=2000`、nggx 無し）

| 確認 | 結果 | 証拠 |
| --- | --- | --- |
| desktop の後の GRBM_STATUS（vk timing の mark） | 0x00003028（SX_BUSY 無し。修正前は起動直後から 0xa0103028）。mview・hang の後の idle も 0x00003028 | 実機の klog の mark（SSH の dmesg） |
| `nggshape --indexed --triangles=16 --map` | ok drawn=58311（修正前 54656） | SSH の出力 |
| 同 `--repeat=1000`（`--map` 付きも） | ok drawn=58311、止まらない（修正前は 4〜5 回目で hang） | 同 |
| mview qs40 1 体の shot（Linux 比） | pixel 差 0（max 0、over15 0） | build/ws291/measure/p005/fix-shot-1b（1 回目の fix-shot-1 は 6 s の時点で mview が未表示で desktop を撮った。mview は 900 frame DONE） |
| mview 6500 体の shot | over15 8 pixel（基準 8 から悪化なし） | build/ws291/measure/p005/fix-shot-6500 |
| desktop の keiland-shot | 正常（壁紙・panel） | fix-hang/desktop.png、fix-shot-1/shot.png |
| `measure.sh hang` | vkhang lost → queue reset 5 ms → vkhang・compute の loops ok、mview 58.4 fps | build/ws291/measure/p005/fix-hang |
| GPU の ms（`--spin=8 --content=game --present=mailbox`、direct scanout） | scene-1 99.4 fps fence 1.22 ms、scene-1500 61.0 fps fence 11.18 ms、scene-6500 22.4 fps fence 41.37 ms（legacy p003: 42.18 ms、21.9 fps） | fix-scene-1・1500・6500 |

- mview の shot の run が `vkAcquireNextImageKHR` の -1000000000（errno 42）で 317 frame 目に終わるのは legacy でも同じ（legacy-shot-1: 316 frame）で、NGG と無関係（shot の取り方の既存の挙動）。
- 既定は legacy のまま（`amdrdna.vk.ngg=1` で NGG）。passthrough の差は 0.8 ms と小さく、既定を NGG にするかは culling（p006・p007）の後に desktop の frame-ops と合わせて決める。
- 前の世代の「残り」1〜4（diag13 の 0x100、PA の register、exp prim の VSRC・FLOAT_MODE、SQ_NON_EVENT）は不要として閉じる（最後の primitive の消失は上の wait で説明・解消した）。VGT_VERTEX_REUSE_BLOCK_CNTL・guard band・binning は性能の段（p008）へ。
- stick の状態（13:49）: /boot/boot0/vmunix = f86cad802a3ab423（修正入り）、zedbsd.cfg は `amdrdna.vk.timing=2000`・`amdrdna.vk.ngg=1`、vmunix.old は 08:37 の物。zedBSD で起動中。

## 残り・再開の条件

- （解決済み、上の「原因と修正」）失われる最後の primitive の原因を見つける。次に試すつもりだった事（それぞれ boot 行の診断 bit で 1 kernel にまとめる）:
  1. 単一 wave の subgroup で、wave の primitive 数（s3[14:8]）と subgroup の数（s2[30:22]）が食い違うかを見る（diag13.patch の bit 0x100: wave 0 で subgroup の primitive が 32 以下なら s2 の数を使う。build 済みの vmunix-diag13、未実機）。
  2. RADV と違う PA の register を RADV の値に: PA_SU_PRIM_FILTER_CNTL 0xc0000000、PA_SC_MODE_CNTL_0 の ALTERNATE_RBS_PER_TILE、guard band、binner。
  3. exp prim の未使用の VSRC を ACO と同じ 0x80 に、FLOAT_MODE を RADV の 0xc0 に。
  4. primitive restart の書き込み前の SQ_NON_EVENT。
- 見つかったら: mview の 1 体・6500 体の shot、desktop、hang の確かめ、GPU の ms を測り、既定を NGG にするか決める。
- 試験の道具（commit 済み）: `nggshape --indexed --triangles=16 [--map] [--probe] [--repeat=N]` が最小の再現。Linux は `gcc -O2 -I<build の nggshape-programs.h の dir> plan/ws291/tests/nggshape/main.c -lvulkan` で build でき、`LD_PRELOAD=ibdump.so IBDUMP=<dir>/ib` で RADV の IB が取れる。
