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
