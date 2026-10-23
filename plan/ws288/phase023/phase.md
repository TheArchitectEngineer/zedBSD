<!-- awesome-plan project=zedbsd record=ws288-p023 -->

# ws288-p023 module の木・空の module・module ごとの spec（part 1: まだ無い module）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §7.6・§8（版 4）。Status: in-progress（part 1 は実装済み、clear の判定は Q1。part 2 = 既存の module の spec は P2）。
- 担当: P1（2026-10-23）。amaterrace-gpu の worktree `/home/awe/amaterrace-worktrees/p1-p023`（branch `p1-p023`、基点 main a217bf8）。この記録は zedBSD の worktree `/home/awe/zedBSD-worktrees/p1-ws288-p023`。
- 決定（Q1 が伝えたユーザーの承認）: U18〜U28・U37・U38 は推しのとおり。SA は延期で `atg_sa` の top の port だけ。KC705 は raster/compute 構成だけ。Fable は使わない（ユーザー 2026-10-23「いったんFableの使用をやめてトップダウンにVerilig実装を進めてください。…」）。

## やったこと（amaterrace-gpu の commit。Q1 が main に merge）

| milestone | commit | 中身 |
| --- | --- | --- |
| 1 | 3df641d | CP の子（`atg_cp_regs/state/ring/seq/fence/ctl/irq/dbg`）、`am_amb_mux`・`am_jtag_regs`、CPU の木（`atg_cpu` の結線と `atg_cpu_core/alu/muldiv/mem/win/dma/fw`）、`atg_cp` の scaffold（子を今の inline の論理の横に配線）と新しい port、`atg_top` の `HAS_CPU`、`spec/README.md`（template・規則・port group の数え方・結線の module の例外）、`tools/lint.sh` |
| 2 | 542a196 | SU の木: `include/atg_su_uop.vh`（decode 済みの命令の field、unit、class、warp-info）と `rtl/gpu/su/` の 24 module（`atg_su` の結線と sched・fetch・icache・dec・sb・rf・opd・alu・fpu・pk・xu・div・exec・lsu・dcache・lds・cbank・tex・exp・lda・lane・rq・wb） |
| 3a | b33f2f7 | VF・RX・TU・ROP の木、`atg_mc_arb`/`atg_mc_aper`（`atg_mc` の scaffold）、`atg_mc_atom`、DC の木（`rtl/dc/`）、`atg_sa` の top だけ、`atg_top` の全 block の parameter と配線（graphics は SU 0 へ）、外の client・register bus・JTAG の口 |
| 3b | cc6843d | board の top: ZYBO `atg_zybo_2dfw/raster/compute/dc` と共有の `lib/atg_zybo_ps`（PS7 の殻）・`atg_zybo_pclk`・`atg_zybo_tmds_out`、KC705 `atg_kc705_raster` と `lib/atg_kc705_mig/clk/jtag`、lint 専用の primitive の stub `sim/lint/xilinx_stubs.v`、`vivado-build.tcl` が `boards/<b>/lib/` も読む |
| 4 | e3c6698 | MC の router の木（2026-10-23 ユーザーの要求、Q1 経由）: `atg_rt`（2 分岐、1 hop 1 段、優先の bit と交互、packet の lock、応答は route の bit で分配）、`am_amb_to_rt`（leaf、client は不変）、`atg_mc_tree`（generate の完全 2 分木、`ROOT_CLIENT` = DC を根元に）、`atg_mc` の root（aperture・VRAM window・AXI master）を書き直し、応答の field を client ごとに、`atg_mc_aper` を実装、`atg_mc_arb` は不要（削除の依頼済み）、`sim/tb/atg_rt_tb.v` 新規、`atg_mc_tb` を 2 つの形に。design.md §4.1 に木の節（この worktree） |

- 生成: port の表と空の module は scratchpad の生成 script（commit しない）で作り、結線は手で書いた。以後の変更は手で行う（script は使わない）。
- 規則（U38）の扱い（Q1 2026-10-23）: AXI・AMB・HCR snoop・window sub-bus・DDR3 の pin は 1 group と数える。結線だけの module（`atg_top`・`atg_cp` の scaffold・`atg_cpu`・`atg_su`・`atg_vf`・`atg_rx`・`atg_tu`・`atg_rop`・`atg_dc`・board の top）は port と行数の上限の例外（`spec/README.md`）。leaf は全部 40 group 以下。
- 設計からの逸脱（全部 spec に書いた）: `atg_su_pkx` は作らない（版 4 §3.9 の 4 で packed の超越を取り下げ、Q1 了承）。追加の module: `atg_cpu_fw`（CPFW の capability。`atg_cpu_win` を 40 port 以下に保つため）、`am_amb_mux`（CP・SU の I$+const・VF・ROP の memory の利用者を 1 client に）、`atg_zybo_ps` 等の board の helper。`atg_mc_atom` は `atg_mc` の子ではなく `atg_top` の中の MC client。µop の payload（DRAW_RAW・DISPATCH・LOAD_CONST）は vendor の STATE 0x1E0〜0x1EE に書いてから kick の pulse（`atg_cp_seq` の port を 40 以下に）。SU の `ex_next`（ROP の次の連番。export の drain の deadlock を避ける）。`atg_top` の新しい port（`xr_*`・`x_req/x_rsp`・`x_vsync`・`j_*`）。MC の応答の field は client ごと（router の木のため）。
- module の一覧（P1 が新しく作った物、行数は 2026-10-23 e3c6698 の時点。CP の子と CPU は以後 P3・P2 が実装中）:

| RTL の directory | module（行数） |
| --- | --- |
| `boards/kc705/` | `atg_kc705_raster`（229） |
| `boards/kc705/lib/` | `atg_kc705_clk`（45）、`atg_kc705_jtag`（45）、`atg_kc705_mig`（109） |
| `boards/zybo/` | `atg_zybo_2dfw`（116）、`atg_zybo_compute`（117）、`atg_zybo_dc`（166）、`atg_zybo_raster`（115） |
| `boards/zybo/lib/` | `atg_zybo_pclk`（40）、`atg_zybo_ps`（275）、`atg_zybo_tmds_out`（41） |
| `include/` | `atg_su_uop`（79） |
| `rtl/common/` | `am_amb_mux`（76）、`am_amb_to_rt`（117）、`am_jtag_regs`（55） |
| `rtl/dc/` | `atg_dc`（185）、`atg_dc_dma`（68）、`atg_dc_i2c`（43）、`atg_dc_regs`（84）、`atg_dc_tgen`（55）、`atg_dc_tmds`（37）、`atg_dc_vga`（43） |
| `rtl/gpu/cp/` | `atg_cp_ctl`（84）、`atg_cp_dbg`（65）、`atg_cp_fence`（88）、`atg_cp_irq`（41）、`atg_cp_regs`（86）、`atg_cp_ring`（103）、`atg_cp_seq`（99）、`atg_cp_state`（59）、`atg_cpu`（162）、`atg_cpu_alu`（69）、`atg_cpu_core`（336）、`atg_cpu_dma`（85）、`atg_cpu_fw`（74）、`atg_cpu_mem`（59）、`atg_cpu_muldiv`（133）、`atg_cpu_win`（97） |
| `rtl/gpu/mc/` | `atg_mc_aper`（37）、`atg_mc_arb`（43）、`atg_mc_atom`（74）、`atg_mc_tree`（218）、`atg_rt`（136） |
| `rtl/gpu/rop/` | `atg_rop`（147）、`atg_rop_blend`（45）、`atg_rop_ds`（81）、`atg_rop_order`（72）、`atg_rop_wb`（79） |
| `rtl/gpu/rx/` | `atg_rx`（122）、`atg_rx_clip`（65）、`atg_rx_order`（37）、`atg_rx_scan`（104）、`atg_rx_setup`（51） |
| `rtl/gpu/sa/` | `atg_sa`（89） |
| `rtl/gpu/su/` | `atg_su`（666）、`atg_su_alu`（69）、`atg_su_cbank`（113）、`atg_su_dcache`（77）、`atg_su_dec`（55）、`atg_su_div`（75）、`atg_su_exec`（87）、`atg_su_exp`（94）、`atg_su_fetch`（58）、`atg_su_fpu`（68）、`atg_su_icache`（65）、`atg_su_lane`（69）、`atg_su_lda`（76）、`atg_su_lds`（77）、`atg_su_lsu`（116）、`atg_su_opd`（99）、`atg_su_pk`（69）、`atg_su_rf`（58）、`atg_su_rq`（66）、`atg_su_sb`（81）、`atg_su_sched`（98）、`atg_su_tex`（98）、`atg_su_wb`（93）、`atg_su_xu`（76） |
| `rtl/gpu/tu/` | `atg_tu`（103）、`atg_tu_addr`（65）、`atg_tu_cache`（87）、`atg_tu_filter`（52） |
| `rtl/gpu/vf/` | `atg_vf`（140）、`atg_vf_asm`（79）、`atg_vf_conv`（54）、`atg_vf_fetch`（82）、`atg_vf_index`（74） |
| `sim/lint/` | `xilinx_stubs`（232） |

  合計 86 module（+ `include/atg_su_uop.vh`、`sim/lint/xilinx_stubs.v`）、spec は各 module の `spec/<path>/<module>.md`（10 節の template）。

## 確認（host、2026-10-23、e3c6698）

- `tools/lint.sh`（Verilator 5.032 `--lint-only -Wall`）: **98 / 98 clean**。rtl の全 module を単独の top、`atg_top` の形: 既定（bitstream 1）、ZYBO（32 byte）、KC705（64 byte、512 bit）、ZYBO 1'（HAS_CPU）、ZYBO 3（RX+ROP）、ZYBO 4（SU 1）、KC705 raster/compute（SU 1 と SU 4: CPU・VF・RX・TU・ROP・外の client・JTAG・stencil・NPHYS 16・NREGS 64・SB 8・packed・subgroup）、SA の予約（SU 1 + `atg_sa`）、board の top 6 本（primitive の stub と `-Wno-PINMISSING -Wno-PINCONNECTEMPTY`、理由は lint.sh の comment）。lint_off は `include/*.vh` の UNUSEDPARAM（既存の規則）、file 名の parameter の stub 3 本の UNUSEDPARAM、lint 専用の primitive の stub だけ。
- `sim/run.sh`（iverilog 12.0、-Wall の警告も FAIL）: **9 / 9 PASS**（am_afifo、am_axi_master、am_axi_regs、atg_cpu_alu・core・muldiv（P2）、atg_mc、atg_rt、atg_top）。atg_mc は seed 1・2・5 で PASS。
- `sim/verilator/run.sh`: PASS（7527 cycle。木の前は 6120: hop の latency の分）。
- 木の試験の強さ（手で mutation を入れて FAIL を確かめ、元に戻した）: 応答を両方の child に出す → atg_mc FAIL、window の検査を外す → atg_mc FAIL、優先の反転・交互の反転・lock を外す → atg_rt FAIL。
- 未実施: Vivado の合成（木の FF の実測、新しい top の bitstream。新しい top の XDC は各 Phase）、板、QEMU（この Phase には無い）。

## 残り・再開点

- Q1: merge（e3c6698。3b までは merge 済み: main 72d744b）。`rtl/gpu/mc/atg_mc_arb.v` と `spec/gpu/mc/atg_mc_arb.md` の削除（不要になった）。P3 の `atg_cp`・`atg_top` の変更との競合（`atg_top` の MC の応答の配線が client ごとの slice になった。CP に `rsp_tag` が付いたら `rsp_tag_v[7:0]` を繋ぐ）。
- Q1 の読み合わせ（受け入れの「port の名前と幅が design.md と合う」）。
- 木の合成の実測（Vivado）と design §3.6 の資源の表への反映（1 hop ≈ line + 30 bit の FF × 2 方向。KC705 で約 34k FF の推定）。
- design.md §7.6 の module の木の表に、追加・削除した module（`atg_cpu_fw`、`am_amb_mux`、`am_amb_to_rt`、`atg_rt`、`atg_mc_tree`、board の lib、`atg_su_pkx` 無し、`atg_mc_arb` 無し）を反映する（Q1 か次の改訂）。
- 規則の違反の記録: 2026-10-23 に P1 が scratchpad の生成の出力の directory を `rm -rf` で 2 回消した（repository・共有の物ではない）。Q1 に報告済み。以後は新しい directory に出力。
