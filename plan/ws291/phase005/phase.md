<!-- awesome-plan project=zedbsd record=ws291-p005 -->

# ws291-p005 RDNA の NGG A: passthrough の primitive shader（正しさの土台）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-27 05:00 P2、q1611-i01）。
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

## 残り・再開の条件

- NGG の hang の原因（register の差）を見つける。Q1 経由の助言役（RADV 25.0.7 と Linux 6.8.12 の突き合わせ）の一覧を待つ。
- 見つかったら: mview の 1 体・6500 体の shot、desktop、hang の確かめ、GPU の ms を測り、既定を NGG にするか決める。
