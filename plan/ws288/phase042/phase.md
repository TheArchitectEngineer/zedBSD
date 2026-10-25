<!-- awesome-plan project=zedbsd record=ws288-p042 -->

# ws288-p042 RTL の階層（bsp・library・gpu）と FP の演算器の重複の整理

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §19.5.5。
- 状態: planned（2026-10-26 05:4x Q1 新設）
- 由来: 2026-10-26 ユーザー「atg_su_fpu atg_rx_fadd atg_su_fadd atg_su_ffma_p このあたりは、core/ primitive/ fpu/ fma/ みたいな階層に分けて、FPGAやASICのプロセスごとに突き詰めて最適化するのもありと思いました。どうでしょうか。すぐやってほしいわけではないです。」「すみません、core/じゃなくてcommon/です。rtl/ bsp/ --> JTAGとか library/ gpu/ みたいなのがいいと思いました。どう思いますか？」「はい、では重複している演算をまとめるのも含めて、そのphaseを追加してください。」
- 依存・順: 実機の検証（p024・p013・p028）の後、p025 の T2（SU の pipeline）の前。名前の移動は多くの file に触れるので、他の担当が amaterrace の RTL を触っていない時に 1 人で行う。

## 範囲（amaterrace-gpu、機能は変えない）

1. 階層: `rtl/common/` を `rtl/library/arith/`（am_add・am_sub・am_addsub・am_shift・am_mul・am_div・am_msb）・`rtl/library/bus/`（am_axi_*・am_amb_*・am_arb3・am_fifo・am_fifo_key・am_afifo・am_sync）・`rtl/bsp/xilinx7/`（am_jtag_regs の BSCANE2 の部分）と `rtl/bsp/generic/`（sim の版）に分ける。`boards/kc705/` は今のまま。
2. FP の演算器の重複をまとめ `rtl/library/fp/` に: fp32 の加算（atg_su_fadd・atg_rx_fadd）、乗算（atg_rx_fmul）、FMA（atg_su_ffma・atg_su_ffma_p・atg_tu_fma、BLT の使い方）、変換と丸め（atg_su_rnd・rint・i2f・f2i・h2f・hrnd、atg_fmt_h2f・f2h など）。段数は parameter、各 module が `localparam LAT` を公開、呼び出し側はその値で valid を合わせる。名前は am_fp_*（案）。
3. 一緒に直す物: `tools/lint.sh`・`sim/run.sh`・`Makefile` の file list、`tools/vivado-*.tcl`、`spec/common/` → `spec/library/`・`spec/bsp/`、`tools/arith-lint.py` の規則（rtl/library/arith と fp の外は 0）、`docs/style.md` の Arithmetic の節、README の階層の説明。
4. 等価の確認: まとめた各 module と元の各 module を毎 cycle の等価の harness で（p037 の build/tmp/equiv/gen.py と fvm2 の形）、generic を基準に。

## 受け入れ

- 新しい階層、`rtl/common/` は無い。FP の重複の各組が 1 つの module に（例外は理由付きの表）。
- 元の module と等価（harness）、make sim・verilator・lint・fw-run・ghci-e2e が PASS（T）、arith-lint 0（新しい規則）。
- KC705 の合成（rebuilt）の LUT・DSP を p037 の後（168,445 / 192）と比べて記録。

## 記録

- 2026-10-26 05:4x Q1: 新設（ユーザーの指示、上の引用）。
