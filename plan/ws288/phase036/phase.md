<!-- awesome-plan project=zedbsd record=ws288-p036 -->

# ws288-p036 KC705 に GPU が入る面積（LUT）

- 親: [WS288](../ws.md)。
- 状態: in-progress（2026-10-25 09:43 q1571 P2: 測定と原因の段）
- 出所: T1-776 の `make oxc-core`（yosys、KC705 の SU の形: NSU 1・SU_NPHYS 16・SU_VARY_WORDS 64、CPU・RX・ROP あり）で **LUT 245,292**（LUT2 69,445・LUT5 68,653 が多い）、FDRE 63,278、RAMB36E1 25・RAMB18E1 53。XC7K325T の LUT は 203,800 なので、yosys の数では 1 SU でも入らない（120%）。p024（板）の前に解く。
- 依存: p030・p027 の RTL（main 5262af6 の候補）。
- 承認: 2026-10-25 ユーザー「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」（最初のゴールの p024・p028 に要る）。製品の形の parameter（NPHYS・FPU の lane の数・SU の数・機能を外す）を変える判断はユーザーに諮る（U 項目）。

## 範囲

1. **測定**: Vivado（`~/opt/amd/2026.1/Vivado`、BASIC の license）の out-of-context の合成で KC705 の core の LUT・FF・BRAM・DSP を測り、yosys の数と比べる（yosys の LUT は多めに出ることがある）。module ごとの内訳（hierarchy の report）。out-of-context の tcl を `tools/` に置く（p025 でも使う）。
2. **原因**: 大きい module の上位（TU の ffilt・etc、ROP の fblend・wpix、SU の FPU・opd、RX の cpoly・setup、BLT、CP の CPU と firmware の memory など）と、LUT2・LUT5 が多い理由（広い mux・組み合わせの道、BRAM・DSP に推論されていない物）。
3. **機能を変えない削減**（P2 が行ってよい）: BRAM・DSP への推論の直し、共有できる演算器の共有、組み合わせの重複の除去、parameter の既定の見直し（機能が同じ物）。
4. **機能や形を変える削減の案**（ユーザーの判断、U 項目）: NPHYS 16 → 8、SU の FPU の lane の数（2026-10-24 ユーザー「FP32は全laneでいいと思います。」の見直しになる）、TU の fp32 の filter を共有にする、などの案を、LUT の見積もりと Vulkan への影響つきで。

## 受け入れ

Vivado の測定の結果と module の内訳、機能を変えない削減の結果（TB は PASS のまま、T に回す）、KC705 に入るかの判定、入らなければ U 項目（推しつき）。

## 記録

- 2026-10-25 09:43 Q1: 新設（T1-776 の oxc-core の結果から）。
- 2026-10-25 11:01 P2 → Q1（測定の段）: Vivado 2026.1 の OOC の合成（xc7k325t、KC705 の board top の parameter と MS4=1、NSU 1、基点 5262af6）で **LUT 421,947（207%）**・FF 86,960・BRAM36 換算 99・DSP 530。T1-776 の yosys と同じ形を Vivado で測ると LUT 298,488（yosys 245k より多い: yosys の過大ではない）。上位: ROP 163k（atg_rop_wpix 74k・atg_rop_fblend 69k、format の pack・unpack が汎用の関数で format が可変）、SU 161k（opd の行 112k は FPU・PK の組み合わせ、FPU 16 lane で約 33k・PK 約 51k）、TU 37.5k、BLT 26k（resolve の px 11k・srgbenc 5.6k）、RX 17k。commit 35ff7b9: FPU の演算器の共有（lane 2,421 → 2,074 LUT、DSP 17 → 6）、PK の fp16 の FMA と compare の共有（lane 3,191 → 1,780 LUT）で SU は推定 −28k LUT・−176 DSP。次は ROP の pack・unpack の format の定数化（機能は同じ）。
- 2026-10-25 11:55 P2 → Q1（削減の途中）: 35ff7b9 → b6b2936（機能は同じ、各 module の TB PASS・lint 0）。module 単体の Vivado の OOC（-flatten none）: ROP 163.3k → 40.5k（atg_rop_wb の line への byte の書き込みが 64 の書き手 × 可変の index で 115.7k → slot ごとの shift と 4 択、wpix と fblend の pack/unpack を定数の format の case に）、BLT 27.5k → 17.0k、SU 161k → 約 126k（FPU・PK・ALU・qd の adder の共有、DSP 446 → 174）。推定の合計（NSU 1）422k → 約 256k（126%）でまだ入らない。全体の再測定と SU_NPHYS=8 の版を合成中。
- 2026-10-25 12:21 P2 → Q1（返却）: 最終 b6b2936（35ff7b9・b6a32ff・000e715・4a1e11e・b6b2936、基点 5262af6）、tools/vivado-ooc.sh・.tcl。Vivado の OOC: 基点 422k（207%）→ **tip 254,377（125%）**、SU_NPHYS=8 で 198,449（97%）。tip の内訳: SU 125.4k、ROP 40.9k、TU 37.8k、RX 17.1k、BLT 15.8k、VF 6.9k、MC 4.2k、CPU 3.3k、CP 2.4k（SU 以外の固定の block 約 129k、design §3.6 の見積もり 13.4k の約 10 倍）。SU 単体: NPHYS16・PACKED・SUBGROUP 123.6k、P0S0 92.4k、NPHYS8・P1S1 69.1k、P0S0 52.8k。機能を変えない削減: ROP 163k → 40.5k（atg_rop_wb の byte の書き込み 115.7k が原因）、BLT 27.5k → 17.0k、FPU・PK・lane・ALU の共有。timing の推定 WNS −32 ns @ 5 ns（p025）。P2 の申告: lint 127、単体 TB 全部 PASS。evidence/ に kc705-r1・kc705-n8 の utilization を写した。T1-786（T1、make sim・verilator）。
  - U80〜U85（ユーザーの判断、P2 の推し）: U80 PACKED 1→0（採用、Vulkan 1.0 は fp16 の算術を求めない、2026-10-24 の決定の見直し）、U81 SUBGROUP 1→0（採用、1.1 の機能、U27 の見直し）、U82 NPHYS 16→8（採用、throughput 半分）、U83 NSU の目標 4〜6 → 当面 1（採用）、U84 fp32 の FPU を一部の lane に（保留、10-24 の「FP32は全lane」の見直し）、U85 ROP の pixel の offset を ps の倍数に限る（採用、GHCI §8.5 が既に求める整列）。U80〜U82 を全部とっても NSU 1 で約 195k（96%、board 込み）で配置配線は厳しい。
- 2026-10-25 12:29 ユーザー「fp16 packedで24k LUTも使ってるのはなぜでしょうか？ laneは8でもOKです」→ **U82 を採用**（KC705 の SU_NPHYS 8）。U80・U81・U83・U84 は未決（PACKED の LUT の理由を Q1 が説明中）。
- 2026-10-25 12:32 ユーザー「次の合成で結果のFmaxを教えてください。」→ P2 に、次の全体の合成で Fmax（1000/(period − WNS)）と critical path の上位 5 本、できれば route の後の値も報告するよう依頼。
- 2026-10-25 12:41 ユーザー「SUのfp16はきちんと２つ同時に計算できる実装を残しつつ、fp32の回路を流用して２回に分けて実行する実装に切り替え可能にして、後者で合成してみましょう。BRAMを意図しているがBRAMになっていない回路がないか、モジュールごとにチェックしてください。浮動小数点回路はすべてDSPを使えているかチェックしてください。直接算術記号から合成せず、DSPに合成できるとわかっている乗算モジュールなどを作って利用します。また、同じモジュール内でDSPが共有できる箇所があると思いますが、それらが独立した算術記号になっていると再利用されないので、このパターンもないかチェックしてください。」→ 範囲に追加（P2 に送った）: (1) PK の切り替えの parameter と後者での合成、(2) BRAM の推論の点検の表、(3) DSP の点検と乗算の module（am_mul など）への置き換え、共有できる乗算の点検の表。
- 2026-10-25 12:57 P2 → Q1（未検証の申告、report は P2 の worktree の build/vivado）: 2d2bd25（KC705 の形、NPHYS 8、NSU 1、flatten none）の全体の合成: LUT 198,666（97.5%）、FF 77,982、BRAM 100.5、DSP 178。合成だけの見積もり WNS −31.55 ns @5 ns → Fmax 約 27.4 MHz。clock は clk の 1 つ。最悪の経路 su/opd/b_warp_reg → su/fpu/pipe_reg[0]、114 段（CARRY4 50・LUT6 39）、36.2 ns（logic 8.9・配線 27.3）。原因は FPU が pipe[0] の前に全部を組み合わせで計算していること（p025 へ）。配置配線（ooc-n8-impl）は実行中。
  - 40e8377: PK_SHARED（atg_top の SU_PK_SHARED、既定 0 = 今の 2 つ同時の形）。shared は FPU の fp32 の FMA で低い半分と高い半分を 2 cycle、round-to-odd の後に fp16 へ 1 回の丸め（二重丸めの 6 例で RNE なら FAIL・RTO なら PASS を確認）。PK の TB 4000 vector、FPU の TB、atg_su_gfx（NPHYS 8・PK_SHARED）PASS。OOC NPHYS 8: PK packed 14,459 LUT / 32 DSP、shared 2,953 LUT / 16 DSP、FPU（packed、共有前）16,929 LUT / 48 DSP。
  - am_mul（rtl/common、use_dsp）を追加し、FPU の lane は FMA の仮数と mul/mulh* で符号付き 33×33 の am_mul 1 つを共有。前は lane に FMA の 24×24 と整数の 33×33 の 2 つ（整数の 4 つの `*` は合成で既に 1 つに共有されていた）、lane あたり 6 DSP。共有後の値は測定中。
  - b008260: atg_fmt_unpack の unorm と正規化器を共有、3,417 → 2,608 LUT。atg_tu_filter 9,220 → 6,887、ffilt 10,892 → 10,234。TB 11 本 PASS・lint clean（申告）。
- 2026-10-25 13:16 Q1: b6b2936（T1-786 PASS）を amaterrace-gpu の main に merge = 481030c（親 f9f817e b6b2936、衝突なし）。merge の後に Q1 が lint rc 0・127 clean、sim-quick rc 0・94 PASS・0 FAIL を確かめた。
- 2026-10-25 13:27 P2 → Q1、Q1 が route_timing_summary.rpt で WNS を確かめた（[evidence/ooc-kc705-n8-routed-timing_summary.rpt](evidence/ooc-kc705-n8-routed-timing_summary.rpt)）: 2d2bd25 の配置配線（OOC、xc7k325tffg900-2、5 ns、clk の 1 つ、MIG・board の clock 無し）: **WNS −34.388 ns → Fmax 約 25.4 MHz**（合成だけの見積もり 27.4 MHz）。TNS −898,940 ns、違反の終点 150,757 / 243,956。WHS +0.060。配線後 LUT 194,412（95.4%）・FF 78,450・BRAM 100.5・DSP 178、overlap 0。最悪の 5 本は全部 TU の fp32 filter（ffilt/k_reg → ffilt/acc_reg、unpack・重み・FMA を 1 cycle、109〜110 段、39.3 ns = logic 13.4・配線 26.0）。合成の block ごとの最悪（slack ns・段）: su/fpu −31.55・114、tu −31.47・97、blt −23.6・84（favg）、su/lane −23.0・75、su/pk −19.0・49、rx −10.8・50、rop −9.36・38、vf −8.06・31、cp −1.31・14、mc −1.15・18、cpu −0.49・14、su/lsu −0.53・16。読み: FP の block は 1 cycle で FP の演算を 1 つ全部行い、中の pipeline が無い → p025（面積ではない）。
- 2026-10-25 13:36 P2 → Q1（申告、未検証）:
  - 配線後の block ごとの最悪（slack ns / 段 / 遅れ ns）: fpu −33.99/118/38.6、tu −34.39/109/39.4、blt −30.4/94/35.4、lane −30.3/70/35.1、pk −26.1/66/31.1、rop −24.0/21/29.0（wb rt_reg → edat）、rx −18.7/45/23.7、vf −15.6/14/20.3、cp −13.3/11/18.3、mc −11.7/10/16.7、lsu −10.5/11/15.5、cpu −6.8/11/11.6。95% の詰まりで短い経路も配線で 10〜12 ns 失う。
  - PK_SHARED と am_mul（OOC NPHYS 8、LUT / DSP）: PK packed 14,459/32、shared 2,953/16。FPU packed・共有前 16,929/48（lane に FMA 2・整数 4 DSP）、am_mul 1 つの共有 18,386/32、2 つ 17,431/48。FPU shared の形: 共有 1 つ 20,774/32、2 つ 21,076/48。PK+FPU の shared の形 23.7k（前 31.4k）、−7.7k LUT・−32 DSP。共有 1 つを採用（d94076c、packed の時は約 950 LUT で 16 DSP を減らす）。TB atg_su_pk・fpu・gfx・su +quick PASS（両方の形）。
  - BRAM の点検: 意図どおり（rf 24・lda 16・pt 8・lds 4・icache 7.5・cbank BRAM18×4・tu cache 7.5・cp state 2・IMEM 16・DMEM 8 BRAM36）。直した物: su/dcache の data 16×512 が LUTRAM（非同期の読み）→ BRAM に（36826c9、2,164 → 1,128 LUT、+7.5 BRAM）。LUTRAM のままが正しい物: exp（spec）、tu/desc（読み 2 つ）、rx/setup・rx/cpoly の pb（多 port、BRAM 化は clipper の作り直しが要る、候補）、各 FIFO。
  - DSP の点検: LUT に落ちた FP の乗算は無し。su/fpu 4/lane（共有後）、pk 2/half、xu 12、tu/ffilt 3、tu/filter 12、tu/addr 10・foot 14・desc 6（pu/qu と pv/qv は cube で排他 → 共有の候補）、rop/blend 17・fblend 2・ds 2・wpix 2、rx 12、blt/favg 0（定数の仮数）、vf/fetch 2、cpu 4。tu/filter の conv40 を am_mul にすると LUT が同じで +16 DSP なので加算器のまま。
  - 追加の削減: 212bb3f・fe4aca5（U85、wpix/wb/ds の offset は ps の倍数、GHCI 8.5）ROP 40,481 → 31,665。fe4aca5 tu_lod を 1 cycle に 1 軸 4,656 → 1,767、TU 37.4k → 31.7k。03626b1・3ab8fe9・dcb6dd3 ffilt の ETC を unpack 経由・FIN を lane ごと・1 cycle に 1 成分、vf_conv も 1 成分ずつ、VF 7,063 → 4,853。d94076c（SU_PK_SHARED=1）の全体の合成を実行中。
- 2026-10-25 13:46 P2 区切り（q1571-i02、p2-p036 d94076c、Q1 が commit を確かめた）。Q1 が Vivado の report で確かめた: d94076c（KC705 の形、NPHYS 8、PK_SHARED 1、NSU 1、flatten none）の合成 **LUT 167,293（82.09%）**、合成の見積もり WNS −39.223 ns → 約 22.9 MHz（ffilt の経路に ETC の decode が入って悪化）。証拠 [evidence/ooc-kc705-fin-utilization.rpt](evidence/ooc-kc705-fin-utilization.rpt)・[_hier](evidence/ooc-kc705-fin-utilization_hier.rpt)・[params](evidence/ooc-kc705-fin-params.txt)。P2 の申告: FF 78,371・BRAM 106.5・DSP 146、block ごと SU 60,272（FPU 20.8k・PK 3.0k・lane 7.5k）・ROP 30,032・TU 27,824・VF 4,747・RX 16,976・BLT 16,751。MIG・DC・XDMA（約 20k）を足して約 187k（92%）の見込み。残り: RX・BLT は未着手、rx cpoly pb の BRAM 化、tu_addr の乗算の共有、TU/ROP/RX/FPU/PK/fmt の am_ の形（p037）、pipeline（p025）。T1-791 に回した。
