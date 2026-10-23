<!-- awesome-plan project=zedbsd record=ws288-p008 -->

# ws288-p008 SU v0 と GSI1 の compute の部分集合（part 1: 整数の core の道）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §3（GSI1・§3.1.1 scoreboard・§3.2 operand・§3.4 符号化）、§3.6、§7.6、§8 の p008 の行。
- 状態: in-progress（2026-10-23 P1: part 1 = 整数の core の道を実装、clear の判定は Q1。受け入れの残り（FPU・memory・LDS・const・超越・除算、`DISPATCH` の結線、板の vector add/reduction、SB_ENTRIES と U12 の実測）は未実施）
- 担当: P1（2026-10-23）。amaterrace-gpu の worktree `/home/awe/amaterrace-worktrees/p1-su`（branch `p1-su`、基点 main a094748。main 98885bf を 50f0c0c で、122817f を 823c22c で merge 済み）。この記録は zedBSD の worktree `/home/awe/zedBSD-worktrees/p1-ws288-p008`。
- 進め方（ユーザー 2026-10-23、Q1 経由）:「トップダウンにVerilig実装を進めてください。スタブ、ドライバの使用は恐れずに、パズルのように部分的に進めてください。」→ module ごとに spec から実装し、各 module に unit の TB を付け、未実装の unit は stub のまま SU の TB で program を流す。Fable は使っていない。

## やったこと（amaterrace-gpu の commit。Q1 が main に merge）

| 段 | commit | 中身 |
| --- | --- | --- |
| 1 | fb92707・06b1da4 | `tools/gsi1asm.py`（GSI1 の assembler と decode の参照表）、`include/atg_su_uop.vh` に unit ごとの OP の値、`atg_su_dec`（64 bit の decode、ISA fault）と TB（4976 vector） |
| 2 | 7b5a404・e960976 | `atg_su_alu`、`atg_su_rf`（3 複製、lane ごとの bank）、`atg_su_wb`、`atg_su_opd`（NPHYS 8/16/32）と各 TB |
| 3 | e266305・fd4aaad | `atg_su_sb`（scoreboard・counter・wait/fence/end・短い結果の interlock、port `x_end` を追加して `atg_su` に結線）と TB |
| 4 | d7966c0・d0480d5・b84b08f | `atg_su_sched`（slot・round-robin・compute の起動・barrier・end）、`atg_su_fetch`、`atg_su_icache`、`atg_su_exec`（EXEC/KILLED・分岐・warp control）と各 TB。`atg_su_exp` は暫定で xr_valid だけ |
| 5 | 612475b | SU 全体の TB `sim/tb/atg_su_tb.v`、ISA の model `tools/gsi1model.py`、program `sim/tb/su/{alu,branch,rand}.s`（`tools/su-tests.sh` で hex と期待値を生成） |
| 6 | 7862390・35ab8a0・514e827 | spec の Status と修正、`atg_su_dec` の表を `atg_su_dec_tab`・`atg_su_dec_tabc` に分割（400 行の規則）、`atg_su_lane` の qd と `include/atg_su_fp.vh`（fp32 加算） |

## 確認（2026-10-23、host の simulation だけ）

- `sim/run.sh`（main 122817f を merge した 823c22c）: 35 本全部 PASS。そのうち SU の TB は 12 本（dec・alu・rf・wb・opd・sb・sched・fetch・icache・exec・lane・atg_su）。
- `atg_su_tb`（p008 の受け入れ「NPHYS 8/16/32 で同じ結果」の整数の部分）: 同じ binary の 3 本の program を NPHYS 8・16・32 の SU で流し、`gsi1model.py` が書いたと言う全部の register の全 lane が一致（alu 128 lane、branch 100 lane（最後の warp は部分）、rand 128 lane で 400 命令の random な整数の program と mask の区間）。cycle 数（alu/branch/rand）: NPHYS 8 で 641/1054/7530、16 で 391/659/4461、32 で 355/566/3840。
- `tools/lint.sh`（823c22c）: 99 件全部 clean。`sim/verilator/run.sh`: PASS（7671 cycle。merge の前の 7862390 で実行）。
- 未実施: 板（ZYBO の bitstream 4）、Vivado の合成（LUT・BRAM の実測、SB_ENTRIES と U12 の確定）、QEMU は対象外。

## 設計・spec からの変更（spec の Status に全部記録）

- `atg_su_sb`: spec に設計 §3.1.1 の「短い結果の interlock」が無かった → parameter `NPHYS`・`SHORT_LAT` と入力 `x_end` を追加。短い結果を書く命令は opd を出てから SHORT_LAT+1 cycle を数え、その rd を読む命令は止める（replay でなく hold）。
- `atg_su_exec`: `exec.save` は lane ごとの EXEC の bit を Boolean で全 lane に書く（spec の「uniform な値」では `exec.set` で戻せない）。wc は最後の group の次の cycle（spec の SHORT_LAT 後から変更）。
- `atg_su_wb`: VF/RX の入力は back-pressure が無いので long の beat より先、16 段の FIFO と wb_stall。
- `atg_su_opd`: q_info に WG_SIZE が無いので v0 は `lidx` = `lidn`、`lidy`/`lidz`/`prim` = 0（1 次元の workgroup では正しい）。p009 で q_info の field を提案。
- `atg_su_dec`: 符号化の未決の点を決めた（rm 7 = RNE、5/6 は fault、custom-2 の funct7 の値、fence は class a など。spec §10 と `gsi1asm.py`）。docs/gsi1.md への転記は残り。
- `atg_su_sched`: 取られなかった fetch は warp を保つ（valid/ready）。

## 残り（次の作業）

- stub のままの unit: `atg_su_fpu`（fp32・mul・fract。`include/atg_su_fp.vh` の加算を使える）、`atg_su_lsu`・`atg_su_dcache`（lw/sw/atom/fence）、`atg_su_lds`、`atg_su_cbank`（lwc・c0-c31・LOAD_CONST）、`atg_su_xu`、`atg_su_div`、`atg_su_pk`、`atg_su_tex`・`atg_su_exp`（export の buffer）・`atg_su_lda`（p009）、`atg_su_rq`（予約）、`atg_su_lane` の shfl/vote/ballot（p009。§10 に interlock の未決の点）。
- `gsi1dis.py`、C の ISA の model（設計 §8 の p008 の行）、docs/gsi1.md。
- `DISPATCH` の CP からの結線と `zybo_compute` の bitstream、板での vector add と reduction、SB_ENTRIES・U12 の実測。
