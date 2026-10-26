<!-- awesome-plan project=zedbsd record=ws288-p042 -->

# ws288-p042 RTL の階層（bsp・library・gpu）と FP の演算器の重複の整理

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §19.5.5。
- 状態: planned（2026-10-26 05:44 Q1 新設）
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

- 2026-10-26 05:44 Q1: 新設（ユーザーの指示、上の引用）。
- 2026-10-26 16:26 ユーザーとの相談（WS288、手順の変更と乗算器）: 順を p042（重複する演算器のまとめ）→ SU の下位の部品の pipeline 化に。演算器の中で `*` による合成をやめる（ユーザー: その記述では 200 MHz の合成が事実上できないことがプロの間で知られている）。決めた事: (1) 幅の広い乗算は DSP48E1 の primitive を直に使い、PCOUT → PCIN の cascade（17 bit の shift、OPMODE・ALUMODE）で部分積を DSP の中で足す（MULT_MACRO の分割と fabric の am_add は取らない）。24×24 unsigned は DSP 2 個、33×33 signed は 4 個の tile（UG479）。(2) LAT の既定は 3（A/B の入力 1 + MREG 1 + PREG 1）、4 は DSP の前段の register を足す形。汎用の版（`*` の記述）も LAT 段の register で cycle まで同じにする。(3) iverilog・Verilator は汎用の版、DSP48E1 の版は unisim（glbl.v と DSP48E1.v）を読ませた iverilog か xsim で乱数の試験をし、汎用の版と同じ cycle で bit 単位で一致することを assertion で比べる試験を 1 本作る。(4) 加算・shifter は fabric のまま（CARRY4・LUT）、DSP に寄せない。shifter は最低 1 段の register を確保する。未決: define の名前（USE_UNIMACRO か USE_DSP48 か）、DC（p045）の位置。
- 2026-10-26 16:30 ユーザー: define の名前は SYNTH_KINTEX7。(i) を採る: p042 では am_mul に DSP48E1 の版を足すだけ（LAT の parameter で 0 = register なし〜3 = 全段を切り替え、LAT 0 は AREG・BREG・MREG・PREG 0 の組合せの形で機能の一致を確かめる移行の段）、汎用の版（`*`）と DSP48E1 の版の一致を単体の sim で。呼び出し側の LAT 3 化は SU → TU → ROP … の順に block ごとの後の Phase で、各 block の timing を Vivado で見ながら。（ユーザーの言う「p043 以降」は番号が既にあるので新しい番号で立てる、Q1。）
