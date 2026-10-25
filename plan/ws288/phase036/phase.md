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
