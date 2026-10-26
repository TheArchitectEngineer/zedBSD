<!-- awesome-plan project=zedbsd record=ws288-p042 -->

# ws288-p042 RTL の階層（bsp・library・gpu）と FP の演算器の重複の整理

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §19.5.5。
- 状態: planned（2026-10-26 05:44 Q1 新設。2026-10-26 範囲を改めた、ユーザーの承認待ち）
- 由来: 2026-10-26 ユーザー「atg_su_fpu atg_rx_fadd atg_su_fadd atg_su_ffma_p このあたりは、core/ primitive/ fpu/ fma/ みたいな階層に分けて、FPGAやASICのプロセスごとに突き詰めて最適化するのもありと思いました。どうでしょうか。すぐやってほしいわけではないです。」「すみません、core/じゃなくてcommon/です。rtl/ bsp/ --> JTAGとか library/ gpu/ みたいなのがいいと思いました。どう思いますか？」「はい、では重複している演算をまとめるのも含めて、そのphaseを追加してください。」
- 依存・順（2026-10-26 16:2x〜16:3x のユーザーとの相談で改めた）: 今すぐ。p042 → SU の下位の部品の pipeline 化（新しい Phase、SU → TU → ROP … の順に block ごと）。DC の 200 MHz（p045）は並べて進める（触る file が分かれる、ユーザー 2026-10-26「両方OKです」）。名前の移動は多くの file に触れるので、p042 の担当以外は amaterrace の rtl/common・FP の module を触らない。
- 状態の補足: 計画をユーザーに見せて承認を得てから担当を起動する（2026-10-26 ユーザー「今から勝手に進めず、私と相談しながら進めましょう。」）。

## 範囲（amaterrace-gpu。機能・cycle は変えない）

1. **階層**: `rtl/common/` を分ける。
   - `rtl/library/arith/`: am_add・am_sub・am_addsub・am_shift・am_mul・am_div・am_msb。
   - `rtl/library/bus/`: am_axi_*・am_amb_*・am_arb3・am_fifo・am_fifo_key・am_afifo・am_sync。
   - `rtl/bsp/xilinx7/` と `rtl/bsp/generic/`: am_jtag_regs の BSCANE2 の部分（Xilinx の primitive）と、その sim の版。
   - `boards/kc705/` は今のまま。`spec/common/` も同じ形に（`spec/library/`・`spec/bsp/`）。
2. **FP の演算器の重複を `rtl/library/fp/` に 1 つずつ**（名前は am_fp_*）。
   - fp32 の加算: atg_su_fadd（組合せ）・atg_rx_fadd（4 段）。
   - 乗算: atg_rx_fmul。
   - FMA: atg_su_ffma・atg_su_ffma_p・atg_tu_fma（10 段）、BLT の atg_su_ffma_p の使い方。
   - 変換と丸め: atg_su_rnd・rint・i2f・f2i・h2f・hrnd、atg_fmt_h2f・f2h など。
   - 段数は parameter。各 module が `localparam LAT` を公開し、呼び出し側は今と同じ cycle になる値で使う（この Phase では段数を変えない）。
3. **`am_mul` の DSP48E1 の版**（`ifdef SYNTH_KINTEX7`）。
   - DSP48E1 の primitive を直に使う。DSP 1 個に入らない幅は PCOUT → PCIN の cascade（17 bit の shift、OPMODE・ALUMODE）で部分積を DSP の中で足す（24×24 unsigned は 2 個、33×33 signed は 4 個、UG479 の形）。fabric の加算で足さない。
   - `LAT` で 0（AREG・BREG・MREG・PREG を全部 0、組合せ）〜 3（A/B の入力 1・MREG 1・PREG 1）を切り替える。4 は DSP の前段の register を足す形（任意）。
   - 汎用の版（`ifndef SYNTH_KINTEX7`、`*` の記述）も同じ `LAT` 段の register を持ち、cycle まで同じ。
   - 小さな乗算（8×8・9×9 など）も全部この版（ユーザー 2026-10-26）。
   - 呼び出し側の `LAT` は今の値のまま（LAT 0 の所は組合せの DSP）。LAT 3 化は後の Phase。
4. **一緒に直す物**: `tools/lint.sh`・`sim/run.sh`・`Makefile` の file list、`tools/vivado-*.tcl`（板の build に `SYNTH_KINTEX7` の define）、`tools/arith-lint.py` の規則（rtl/library/arith と fp の外は 0）、`docs/style.md` の Arithmetic の節（`*` は am_mul の汎用の版の中だけ、板は DSP48E1 の版）、README の階層の説明。加算・shifter は fabric のまま（CARRY4・LUT、DSP に寄せない）。

## 確かめ

1. まとめた各 FP の module と元の各 module を、毎 cycle の等価の harness で比べる（p037 の build/tmp/equiv/gen.py と fvm2 の形）。
2. `am_mul` の汎用の版と DSP48E1 の版を、unisim（`$XILINX_VIVADO/data/verilog/src/unisims/` の DSP48E1.v と glbl.v）を読ませた iverilog（か xsim）で、乱数の入力で同じ cycle で bit 単位で一致するか assertion で比べる試験を 1 本（使っている全部の幅・SIGNED・LAT 0〜3）。
3. iverilog・Verilator の日常の試験は汎用の版のまま: make sim・verilator・lint・fw-run・ghci-e2e が PASS（T）、arith-lint 0（新しい規則）。
4. KC705 の板の build（`SYNTH_KINTEX7`、Vivado 2026.1 build 6511674、Default、MIG_DEBUG 0）: LUT・DSP を p037 の後と比べ、DSP48E1 の数と各 DSP の register の設定（report_utilization・DSP の property）を記録。build の warning は全部の一覧をユーザーに見せる（2026-10-26 のユーザーの指示）。

## 受け入れ

- 新しい階層、`rtl/common/` は無い。FP の重複の各組が 1 つの module に（例外は理由付きの表）。
- `am_mul` の 2 つの版が全部の形で bit・cycle で一致（試験 2）。
- 確かめ 1・3 が PASS、板の build の結果と warning の一覧を記録。

## 記録

- 2026-10-26 05:44 Q1: 新設（ユーザーの指示、上の引用）。
- 2026-10-26 16:26 ユーザーとの相談（WS288、手順の変更と乗算器）: 順を p042（重複する演算器のまとめ）→ SU の下位の部品の pipeline 化に。演算器の中で `*` による合成をやめる（ユーザー: その記述では 200 MHz の合成が事実上できないことがプロの間で知られている）。決めた事: (1) 幅の広い乗算は DSP48E1 の primitive を直に使い、PCOUT → PCIN の cascade（17 bit の shift、OPMODE・ALUMODE）で部分積を DSP の中で足す（MULT_MACRO の分割と fabric の am_add は取らない）。24×24 unsigned は DSP 2 個、33×33 signed は 4 個の tile（UG479）。(2) LAT の既定は 3（A/B の入力 1 + MREG 1 + PREG 1）、4 は DSP の前段の register を足す形。汎用の版（`*` の記述）も LAT 段の register で cycle まで同じにする。(3) iverilog・Verilator は汎用の版、DSP48E1 の版は unisim（glbl.v と DSP48E1.v）を読ませた iverilog か xsim で乱数の試験をし、汎用の版と同じ cycle で bit 単位で一致することを assertion で比べる試験を 1 本作る。(4) 加算・shifter は fabric のまま（CARRY4・LUT）、DSP に寄せない。shifter は最低 1 段の register を確保する。未決: define の名前（USE_UNIMACRO か USE_DSP48 か）、DC（p045）の位置。
- 2026-10-26 16:30 ユーザー: define の名前は SYNTH_KINTEX7。(i) を採る: p042 では am_mul に DSP48E1 の版を足すだけ（LAT の parameter で 0 = register なし〜3 = 全段を切り替え、LAT 0 は AREG・BREG・MREG・PREG 0 の組合せの形で機能の一致を確かめる移行の段）、汎用の版（`*`）と DSP48E1 の版の一致を単体の sim で。呼び出し側の LAT 3 化は SU → TU → ROP … の順に block ごとの後の Phase で、各 block の timing を Vivado で見ながら。（ユーザーの言う「p043 以降」は番号が既にあるので新しい番号で立てる、Q1。）
