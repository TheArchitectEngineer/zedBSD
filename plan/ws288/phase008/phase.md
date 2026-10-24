<!-- awesome-plan project=zedbsd record=ws288-p008 -->

# ws288-p008 SU v0 と GSI1 の compute の部分集合（part 1: 整数の core の道）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §3（GSI1・§3.1.1 scoreboard・§3.2 operand・§3.4 符号化）、§3.6、§7.6、§8 の p008 の行。
- 状態: in-progress（2026-10-23 P1: part 1 = 整数の core の道。2026-10-24 P1: part 2 = FPU・PK・XU・DIV・LSU・D$・LDS・cbank と SU 全体の FP/memory の program。clear の判定は Q1。残り: ZYBO に入らない（下の Vivado）、板の vector add/reduction、SB_ENTRIES と U12 の実測）
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

## part 2（2026-10-24 P1、ユーザーの「10 分でまとめ」の指示で途中で止めた）

amaterrace-gpu の worktree `/home/awe/amaterrace-worktrees/p1-su2`（branch `p1-su2`、基点 main d5e4df8）。commit: 1fc2dc1（FPU・DIV・XU、`tools/gsi1fp.py`）、9d7fe0d（D$・LSU・LDS・cbank）、56a28ea（PK、spec の Status）、e3f8307（`tools/gsi1model.py` の FP/XU/DIV/memory/LDS/const/bar、SU TB の program 7 本、`docs/gsi1.md`、`atg_zybo_compute.xdc`、`sim/run.sh` の `SIM_TIMEOUT`）。

| module | 中身 | TB |
| --- | --- | --- |
| `atg_su_fpu` | fp32 の fused datapath（`include/atg_su_fp.vh` の `atg_su_ffma`）、rm 0-4/7、flush、fmin/fmax・比較（Boolean）・fcvt・fround・fract・sat、mul* | 6000 vector（`gsi1fp.py --fpu-vec`、厳密な有理数の参照）、わざと誤らせて FAIL を確認 |
| `atg_su_pk` | packh/unpackh/swaph（常に）、PACKED で fp16 の fma 等（`include/atg_su_fp16.vh`） | 4000 vector、PACKED 1 と 0 |
| `atg_su_xu` | rcp/rsq/exp2/log2/sin/cos/fsqrt を整数の逐次 engine で（model と bit 一致、rcp/fsqrt は正しく丸め、rsq・exp2 ≤ 1 ulp、log2 は絶対 2^-22、sin/cos は絶対 2^-24）。定数は `include/atg_su_xu_tab.vh`（生成） | 3000 vector、bit 一致と実関数との誤差 |
| `atg_su_div` | 32 段の restoring、RISC-V の 0 除算・INT_MIN/-1 | 400 group |
| `atg_su_dcache`・`atg_su_lsu`・`atg_su_lds`・`atg_su_cbank` | D$ 1 KiB write-through、coalesce（line ごとに 1 要求）・sw の完了 beat・atom（at_*）・fence acquire、LDS、LOAD_CONST と c operand・lwc | 各 TB（amb_mem、atom の model） |

確認（2026-10-24、host の simulation だけ）:
- `sim/run.sh`（e3f8307、SIM_TIMEOUT=1700）: 50 本全部 PASS。`atg_su_tb`: 7 program（alu・branch・rand・frand（FP/XU/DIV の random 400 命令）・mem（lw/sw/atom 11 種/fence/lwc/c operand/LDS+bar/kill）・vadd（2 workgroup）・reduce（LDS の木 + bar））を NPHYS 8・16・32 で、register と data memory の全部が `gsi1model.py` と一致。
- `tools/lint.sh`・Verilator: 個々の新 module は lint clean を確認。全体の `tools/lint.sh` と `sim/verilator/run.sh` は流し始めたところで止めた（未確認）。
- **Vivado（atg_zybo_compute、xc7z010）: 入らない**。LUT 72,105（17,600 の 4 倍）、CARRY4 7,501、DSP 86（80）で place 前の DRC で失敗。原因の見込み: FPU の lane ごとの組み合わせの fused datapath（77 bit の加算と正規化 shift）× NPHYS 8、PK・XU も組み合わせ。未解析（utilization の module 別は未取得）。

spec からの違い（各 spec の Status に記録）: XU・DIV は lane group ごとに受ける（spec は命令全体を buffer）ので遅い命令の間 operand stage が止まる。XU は 1 lane 30〜60 cycle（spec は 1 lane/cycle）。lwc・lds は 1 lane/cycle。`atg_mc_atom` は stub のままなので板では atom が止まる（reduce.s から atom を外した）。

再開点（次の作業）:
1. Vivado の module 別の utilization（`report_utilization -hierarchical`）で LUT の内訳を取る。FPU を lane ごとに 1 本の組み合わせから、pipeline（SHORT_LAT の段に分割）と共有に直す、NPHYS 4 の構成、FPU の lane 数を NPHYS より少なくする案などを Q1 と相談（design §3.6 の見積もり 10.1k との差が大きい）。
2. 全体の `tools/lint.sh`・`sim/verilator/run.sh` を流す。
3. 入ったら板: `plan/ws288/tests/ghci-su-board.py`（このcommit で追加、未実行。fpgareg の DBG で code と data を書き、KICK bit 2 で DISPATCH、STATUS.SU_BUSY を待って gsi1model と比較）。
4. SB_ENTRIES・U12 の実測、XU/DIV の命令 buffer、`gsi1dis.py`。
- Q1 への依頼: worktree `/home/awe/amaterrace-worktrees/p1-su2/tools/__pycache__`（git 管理外）の削除。

## 2026-10-24 Q1: ZYBO の compute の bitstream が入らない原因（Vivado 2026.1、`synth_design -flatten_hierarchy none` の hierarchical の使用量、NPHYS 8・NREGS 32・NWARPS 4）

| module | LUT |
| --- | --- |
| SU 全体 | 70,735 |
| fpu（8 lane、1 lane ≈ 4.9k の組み合わせの fp32 の datapath） | 39,281 |
| lane | 6,472 |
| xu | 5,983 |
| alu（8 lane） | 5,154 |
| lsu | 2,560 |
| opd | 2,245 |
| pk / wb / dcache / sb / sched | 1,762 / 1,479 / 1,163 / 1,151 / 848 |

- 既定（flatten rebuilt）の報告では opd が 43k に見えたが、それは他の unit の論理が混ざった値。真の主因は FPU（7010 の 17,600 LUT の 2 倍以上を FPU だけで使う）。
- Q1 は opd の lane に依らない部分（定数・warp と workgroup の値・定数の word）を operand ごとに 1 回だけ作る形に直した（amaterrace-gpu、LUT はほぼ変わらず、TB PASS）。
- 収め方の候補（ユーザーと決める。SU 全体で約 1/5 にする必要）: (a) FPU の物理の lane を NPHYS より少なく（例 FPU は 1〜2 lane で 32 lane を順に流す）、(b) FPU を SHORT_LAT の段に pipeline して乗算を DSP に寄せる、(c) ZYBO の compute の構成は NPHYS 1〜2、(d) xu・lane・alu も同じく lane を絞る。design §3.6 の見積もり（SU 10.1k LUT）は FPU を大きく見誤っていた。
- 2026-10-24 Q1: 残件の `atg_mc_atom`（MC の atomic の unit、p023 の stub のまま。SU の atom は板・sim で止まる）を P1 に依頼（ユーザー「次の1つを進めます」）。worktree `~/amaterrace-worktrees/p1-atom`（branch `p1-atom`、基点 main b181b44）。範囲: spec から実装と単体 TB、MC の結線の確認、SU の atom の program が model と一致する sim、古い「p023 stub」のヘッダの表記の整理（exec・fetch・icache・lane・sched・wb・rq）。
- 2026-10-24 P1 → Q1（`atg_mc_atom` の結果）: amaterrace-gpu 6bc02ff（親 b181b44）。`atg_mc_atom` を spec から実装（IDLE → RREQ → RWAIT → WREQ → WWAIT の直列の read-modify-write、op 0〜10 は ghci §12.5 の atom、その word の byte enable だけを書き戻す、旧値は write の応答の後、要求元は round-robin）。単体 TB `atg_mc_atom_tb`（32・64 byte の line、ops と 2 要求元の競合）、`atg_su_tb` の振る舞いの model の atom を実物に替えた、`mem.s` の不一致の cmpxchg・`reduce.s` の 128 lane の `atom.add`（`tools/su-tests.sh` で hex・exp を作り直し）。実装済みの SU の module のヘッダの古い「p023 stub」の表記を直した（lane は shfl/vote/ballot がまだ結果を返さないと明記）。P1 の申告: lint clean、`sim/run.sh` 全体 75/75 PASS（SIM_TIMEOUT=1700）、Verilator 4 本 PASS、故意の誤りで TB が FAIL。
  - Q1 の確認: 6bc02ff は commit（WIP）。worktree で `atg_mc_atom` PASS 3 行・`atg_su_lsu` PASS・`atg_su`（SIM_TIMEOUT=1700）nphys8・16・32 と本体の PASS。main（4a56fbc）へ `--no-ff` で merge、衝突なし、merge の後に lint clean、`atg_mc_atom`・`atg_top_tex` PASS で 2a13d43（親 4a56fbc 6bc02ff）。P1 の案の atom の順序と不可分性を `docs/ghci.md` §12.8 に書いた（4a56fbc）。push はしていない。
  - spec の読み（Q1 が承認）: round-robin は最後に受けた要求元の次から、op 11〜15 は旧値を書き戻す、read の error は書かずに at_error と旧値 0、write の error は at_error と旧値、ADDR_W は 32 だけ。
  - p008 の残り: `shfl`/`vote`/`ballot` が結果を返さない（`atg_su_lane`。CAPS[24] の構成の実装）、`gsi1dis.py`、CP の DISPATCH の端から端（sim）。板（`ghci-su-board.py`、reduce.s の期待値が変わった）は p024、SB_ENTRIES・U12 の実測（合成）は p025。atg_top の階層で compute の atom を流す TB は無い（未実施）。
- 2026-10-24 Q1（ユーザー「p008 の残りをお願いします。」）: 残りを 2 つに分けて依頼。P1: `shfl`/`vote`/`ballot`（subgroup、CAPS[24]）の結果を返す実装（worktree `~/amaterrace-worktrees/p1-subgroup`、branch `p1-subgroup`）。P2: `tools/gsi1dis.py` と CP の DISPATCH から compute の端から端の sim（worktree `~/amaterrace-worktrees/p2-dispatch`、branch `p2-dispatch`）。どちらも基点 main dbdf6e7。板は p024、SB_ENTRIES・U12 の実測は p025。
