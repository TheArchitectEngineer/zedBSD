<!-- awesome-plan project=zedbsd record=ws288-p042 -->

# ws288-p042 RTL の階層（bsp・library・gpu）と FP の演算器の重複の整理

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §19.5.5。作業手順書: [work-spec.md](work-spec.md)（2026-10-26 17:08 ユーザー添付の原文）。
- 状態: planned（2026-10-26 05:44 Q1 新設。2026-10-26 範囲・詳細を改めた、ユーザーの承認待ち）
- 由来: 2026-10-26 ユーザー「atg_su_fpu atg_rx_fadd atg_su_fadd atg_su_ffma_p このあたりは、core/ primitive/ fpu/ fma/ みたいな階層に分けて、FPGAやASICのプロセスごとに突き詰めて最適化するのもありと思いました。どうでしょうか。すぐやってほしいわけではないです。」「すみません、core/じゃなくてcommon/です。rtl/ bsp/ --> JTAGとか library/ gpu/ みたいなのがいいと思いました。どう思いますか？」「はい、では重複している演算をまとめるのも含めて、そのphaseを追加してください。」
- 依存・順: 今すぐ。p042 → SU の下位の部品の pipeline 化（新しい Phase、SU → TU → ROP … の順に block ごと）。DC の 200 MHz（p045）は並べて進める。名前の移動は多くの file に触れるので、p042 の担当以外は amaterrace の rtl/common・FP の module を触らない。
- 進め方（2 段階のマイルストーン）:
  - Step A（階層整理と FP 統合）: 範囲 1・2・4（SYNTH_KINTEX7 以外）を実施し、確かめ 1・3（等価検証・日常 sim/lint・arith-lint 0）がすべて PASS することを確認して中間記録・コミット境界とする。
  - Step B（am_mul の DSP48E1 カスケード化と板ビルド）: 範囲 3 と Vivado スクリプトの SYNTH_KINTEX7 追加を実施し、確かめ 2・3・4 を完了する。
- 状態の補足: 計画をユーザーに見せて承認を得てから担当を起動する（2026-10-26 ユーザー「今から勝手に進めず、私と相談しながら進めましょう。」）。

## 範囲（amaterrace-gpu。機能・cycle は変えない）

1. 階層: rtl/common/ を廃止して分ける。
   - rtl/library/arith/（7 file）: am_add・am_sub・am_addsub・am_shift・am_mul・am_div・am_msb。
   - rtl/library/bus/（13 file）: am_axi_{cdc,gate,idtrack,master,regs,split}・am_amb_{mux,to_rt}・am_arb3・am_fifo・am_fifo_key・am_afifo・am_sync。
     - am_axi_master.v 等の内部に残っていたカウンタの生 `+`/`-` も am_add/am_sub に置き換え、arith の外の生演算子を 0 にする。
   - rtl/bsp/xilinx7/ と rtl/bsp/generic/:
     - JTAG レジスタブリッジ本体 am_jtag_regs と、BSCANE2 + BUFG の TAP ラッパ（Xilinx 7 シリーズ primitive 版と sim/generic 版）。
     - boards/kc705/lib/atg_kc705_jtag.v の階層名（dut.jtag.bscan.scan / idle）は維持し、atg_kc705_raster_tb.v の階層参照を壊さない。
   - boards/kc705/ は今のまま。spec/common/ も同じ形に（spec/library/arith/・spec/library/bus/・spec/library/fp/・spec/bsp/）。
2. FP の演算器の重複を rtl/library/fp/ に 1 つずつ（名前は am_fp_*、全 10 module）。
   - 段数は parameter LAT（既定値は主用途の段数）。各 module が localparam LAT_OUT = LAT を公開し、呼び出し側は今と同じ cycle になる値で使う（この Phase では段数を変えない）。
   - fp32 の加算 `am_fp_add #(.LAT(0 or 4))`:
     - 統合元: atg_su_fadd（LAT=0、組合せ）と atg_rx_fadd（LAT=4、4 段）。どちらも RNE・入出力 denormal flush・canonical NaN 0x7FC00000 でビット完全一致。
     - 置換先: LAT=0 は atg_su_lane（qdx/qdy の 2 個/quad）、LAT=4 は atg_rx_{setup,cpoly,scan,bary}・atg_rop_{ms,fblend}・atg_rx_fp_tb。
   - fp32 の乗算 `am_fp_mul #(.LAT(0 or 4))`:
     - 統合元: atg_rx_fmul（LAT=4、内部で 24×24 am_mul を使用）。LAT=0 も generate で対応。
     - 置換先: LAT=4 で atg_rx_{setup,cpoly,scan,bary}・atg_rop_fblend・atg_rx_fp_tb。
   - fp32 の FMA `am_fp_fma_p #(.LAT(0))` と `am_fp_fma #(.LAT(0 or 10), .TW(16))`:
     - `am_fp_fma_p`: 統合元は atg_su_ffma_p（LAT=0、48-bit 仮数積 p を外部から入力、rm 0..6・noc 対応）。置換先は atg_su_fpu_op（レーン共有の 33×33 am_mul の積を入力）および atg_blt_favg（fb = 1.0 / 0.25 のため乗算器なしで p = {1'b0, ma, 23'd0} を直結）。
     - `am_fp_fma`: 統合元は atg_su_ffma（LAT=0、24×24 am_mul + am_fp_fma_p）と atg_tu_fma（LAT=10、24×24 am_mul LAT=3 + cycle 3 での c_val 遅延読み出し c_vld/c_tag + RNE 丸め）。置換先は atg_tu_ffilt（LAT=10）と atg_tu_fma_tb。
   - 丸めと変換:
     - `am_fp_rnd #(.LAT(0))`: 旧 atg_su_rnd（正規化済み s, e, m, g, st, rm → fp32、rm 5/6 の RNO 含む）。置換先は atg_su_fpu_op・am_fp_i2f・atg_su_div。
     - `am_fp_rint #(.LAT(0))`: 旧 atg_su_rint（fround/ffloor/fceil/ftrunc）。置換先は atg_su_fpu_op。
     - `am_fp_i2f #(.LAT(0))`: 旧 atg_su_i2f（32-bit 絶対値 + 符号 + rm → fp32）。置換先は atg_su_fpu_op。
     - `am_fp_f2i #(.LAT(0))`: 旧 atg_su_f2i（fp32 → 32-bit signed/unsigned 整数）。置換先は atg_su_fpu_op。
     - `am_fp_h2f #(.DENORM(0 or 1), .LAT(0))`:
       - DENORM=0（旧 atg_su_h2f、fp16 denormal を ±0 に flush、design §3.1）: 置換先は atg_su_{fpu,fpu_op,pk,pk_half}。
       - DENORM=1（旧 atg_fmt_h2f、fp16 denormal を正規化して保持、ghci §8.5）: 置換先は atg_blt_favg・atg_tu_{fword,ffilt}。
     - `am_fp_f2h #(.DENORM(1), .LAT(0))` と `am_fp_hrnd #(.LAT(0))`:
       - `am_fp_f2h`（旧 atg_fmt_f2h、RNE・fp16 denormal 生成）: 置換先は atg_fmt_pack・atg_blt_favg・atg_rop_fblend。
       - `am_fp_hrnd`（旧 atg_su_hrnd、展開済み s, e, m, g, st, rm → fp16・denormal flush）: 置換先は atg_su_{fpu,pk_half}。
   - 例外（統合せず各 block に残す module、理由付き）:
     - atg_su_div / atg_rx_fdiv（SU は NPHYS レーン直列 + 整数/FP 除算で am_div 共有 + 全 rm 対応、RX は 26-bit 固定回復ステップ・RNE 専用で構造が異なる）
     - atg_su_xu（超越関数 FSM・多項式テーブルで SU 固有）
     - atg_su_pk_half（FP16 FMA と INT16 演算で 16×16 am_mul・36-bit 加減算を共有）
     - atg_rx_i2f（W<=23 固定小数点 → 丸めなし exact fp32 の軽量変換）
     - atg_rx_f2fix / atg_tu_{coord,unnorm,fx32,layer}（各ステージ固有の固定小数点幅・飽和シフト）
     - atg_fmt_{comp,cvt}（SFLOAT16 / UFLOAT / E5B9G9R9 / UNORM で 10-bit 正規化器を共有）
     - atg_fmt_f2unorm / atg_rop_f2u8 / atg_fmt_depth_u24（UNORM 変換群）
3. am_mul の DSP48E1 の版（ifdef SYNTH_KINTEX7）。
   - DSP48E1 の primitive を直に使う。DSP 1 個（25×18 signed）に入らない幅は PCOUT → PCIN の cascade（17 bit unsigned タイル分割、OPMODE[6:4]=3'b101 の 17-bit 右シフト加算および 3'b001 の同重み加算、ALUMODE=4'b0000）で部分積を DSP の中で足す。fabric の加算で足さない。
   - RTL 内の全インスタンス幅をカバーするタイル構成:
     - 1 DSP（A_S <= 25 && B_S <= 18）: 8×8, AW(6..9)×8, 9×9, 16×8, 8×17, 16×16, 17×16, 16×17, 24×17 unsigned、および am_arith_tb の 17×13 unsigned/signed。
     - 2 DSP 1×2 カスケード（片軸 <= 25 signed、他軸 19..35 signed → 17-bit unsigned + 上位）: 18×18, 24×24, 32×3, 32×16, 17×32 unsigned、および 25×25, 27×17, 30×17 signed。
     - 4 DSP 1×4 カスケード（24×59 unsigned、atg_su_xu u_tp）: A=24 unsigned、B=17×3+8 の 4 段カスケード。
     - 4 DSP 2×2 カスケード（33×33 signed、atg_su_fpu・atg_cpu_muldiv）: A=17+16 signed、B=17+16 signed の 4 段カスケード（UG479）。
     - 6 DSP 2×3 カスケード（42×42 unsigned、atg_su_xu u_prod）: A=17+24 unsigned（上位 24-bit unsigned は 25-bit signed A ポートに収まる）、B=17+17+8 unsigned の 6 段カスケード。
   - LAT で 0（AREG・BREG・MREG・PREG を全部 0、組合せ）〜 3（入力段・MREG・PREG を配分し、カスケードの最終出力と途中タイルからの下位 17-bit 引き出しの双方がきっかり LAT cycle になる構成）を切り替える。en は全レジスタ段の CE に接続。
   - 汎用の版（ifndef SYNTH_KINTEX7、* の記述）も同じ LAT 段の register を持ち、cycle・en 挙動まで完全一致。
   - 呼び出し側の LAT は今の値のまま（LAT=0: SU/RX/ROP/BLT/DC/TU アドレス系、LAT=1: atg_cpu_muldiv・atg_tu_{ffront,fpre}、LAT=2: atg_tu_fsum、LAT=3: atg_tu_fma）。LAT 3 化は後の Phase。
4. 一緒に直す物:
   - tools/lint.sh・sim/run.sh・Makefile の file list。
   - tools/vivado-build.tcl・tools/vivado-ooc.tcl（synth_design に `-verilog_define SYNTH_KINTEX7` を追加、DSP48E1 の個数と各 DSP の AREG/BREG/MREG/PREG レポート出力を追加）。
   - tools/arith-lint.py の規則（rtl/library/arith の外は算術演算子 0）。
   - docs/style.md の Arithmetic の節（* は am_mul の汎用の版の中だけ、板は DSP48E1 の版）、README.md・spec/README.md の階層の説明。加算・shifter は fabric のまま（CARRY4・LUT、DSP に寄せない）。

## 確かめ

1. まとめた各 FP の module と元の各 module を、毎 cycle の等価の harness で比べる（p037 の build/tmp/equiv/gen.py と fvm2 の形、1,000,000 乱数 + 特殊値・桁落ち）。
2. am_mul の汎用の版と DSP48E1 の版を、unisim（$XILINX_VIVADO/data/verilog/src/unisims/DSP48E1.v と $XILINX_VIVADO/data/verilog/src/glbl.v、トップに glbl を並立し GSR 解除の 100 ns 経過後に駆動；必要に応じ xsim バッチも併用）を読ませた iverilog（か xsim）で、乱数の入力とランダムな en で同じ cycle で bit 単位で一致するか assertion で比べる単体試験を 1 本（使っている全部の幅・SIGNED・LAT 0〜3）。
3. iverilog・Verilator の日常の試験は汎用の版のまま: make sim・verilator・lint・fw-run・ghci-e2e が PASS（T）、arith-lint 0（新しい規則）。
4. KC705 の板の build（SYNTH_KINTEX7、Vivado 2026.1 build 6511674、Default、MIG_DEBUG 0）: LUT・DSP を p037 の後と比べ、DSP48E1 の数と各 DSP の register の設定（report_utilization・DSP の property）を記録。build の warning は全部の一覧をユーザーに見せる。
   - p042 では LAT=0 の DSP カスケードが残るため、該当パスの負の WNS（タイミング違反）は許容し、タイミング収束は後続の block 別パイプライン化 Phase の責務とする。

## 受け入れ

- 新しい階層、rtl/common/ は無い。FP の重複の各組が 1 つの module に（例外は理由付きの表）。
- am_mul の 2 つの版が全部の形で bit・cycle で一致（試験 2）。
- 確かめ 1・3 が PASS、板の build の完走結果（LUT・DSP・レジスタ設定）と warning の全件一覧を記録。

## 計画の注意（2026-10-26 17:08 Q1、ユーザーの確認待ち）

上の範囲・確かめ・受け入れと [work-spec.md](work-spec.md) はユーザーの原文のまま。amaterrace main c7af33f と照らして、次の食い違いがある。

1. **42×42 unsigned の DSP の数**: 範囲 3 は「6 DSP 2×3、A=17+24」だが、17+24 は 41 bit で 42 bit に 1 bit 足りない（A の上位 tile は unsigned 24 bit まで）。work-spec §2.5 の Case 5 の行は途中で切れており、続く IMPORTANT の注と §2.5 (3) は 3×3 = 9 DSP を結論にしている。
2. **module の数**: 範囲 2 は「全 10 module」、work-spec §1.3 は「新設 10・削除 13」。表に並ぶのは新設 11（add・mul・fma_p・fma・rnd・rint・i2f・f2i・h2f・f2h・hrnd）、削除 15。c7af33f には 15 の旧 file がすべてある。
3. **am_fp_f2h の DENORM=0**: 範囲 2 は DENORM(1) だけ、work-spec §1.3 は「DENORM=0 対応」。DENORM=0 の使い手は無い。
4. work-spec のリンクはユーザーの手元の Windows の checkout（file:///c:/Work/amaterrace-gpu/）を指す。この host では ~/amaterrace-gpu。行番号は c7af33f と照らす。

確認済み（c7af33f）: rtl/common は 21 file（arith 7・bus 13・am_jtag_regs）、sim/lint/xilinx_stubs.v はある。am_mul の既定は LAT 0。

## 記録

- 2026-10-26 05:44 Q1: 新設（ユーザーの指示、上の引用）。
- 2026-10-26 16:26 ユーザーとの相談（WS288、手順の変更と乗算器）: 順を p042（重複する演算器のまとめ）→ SU の下位の部品の pipeline 化に。演算器の中で `*` による合成をやめる（ユーザー: その記述では 200 MHz の合成が事実上できないことがプロの間で知られている）。決めた事: (1) 幅の広い乗算は DSP48E1 の primitive を直に使い、PCOUT → PCIN の cascade（17 bit の shift、OPMODE・ALUMODE）で部分積を DSP の中で足す（MULT_MACRO の分割と fabric の am_add は取らない）。24×24 unsigned は DSP 2 個、33×33 signed は 4 個の tile（UG479）。(2) LAT の既定は 3（A/B の入力 1 + MREG 1 + PREG 1）、4 は DSP の前段の register を足す形。汎用の版（`*` の記述）も LAT 段の register で cycle まで同じにする。(3) iverilog・Verilator は汎用の版、DSP48E1 の版は unisim（glbl.v と DSP48E1.v）を読ませた iverilog か xsim で乱数の試験をし、汎用の版と同じ cycle で bit 単位で一致することを assertion で比べる試験を 1 本作る。(4) 加算・shifter は fabric のまま（CARRY4・LUT）、DSP に寄せない。shifter は最低 1 段の register を確保する。未決: define の名前（USE_UNIMACRO か USE_DSP48 か）、DC（p045）の位置。
- 2026-10-26 16:30 ユーザー: define の名前は SYNTH_KINTEX7。(i) を採る: p042 では am_mul に DSP48E1 の版を足すだけ（LAT の parameter で 0 = register なし〜3 = 全段を切り替え、LAT 0 は AREG・BREG・MREG・PREG 0 の組合せの形で機能の一致を確かめる移行の段）、汎用の版（`*`）と DSP48E1 の版の一致を単体の sim で。呼び出し側の LAT 3 化は SU → TU → ROP … の順に block ごとの後の Phase で、各 block の timing を Vivado で見ながら。（ユーザーの言う「p043 以降」は番号が既にあるので新しい番号で立てる、Q1。）
- 2026-10-26 17:08 ユーザー「計画を更新します。phase定義書と、今回の作業手順書を添付します。」→ 範囲・確かめ・受け入れ・進め方（Step A・B）を添付の phase 定義書に置き換え、作業手順書を [work-spec.md](work-spec.md) に原文で保存（Q1）。承認と担当の起動は未。
