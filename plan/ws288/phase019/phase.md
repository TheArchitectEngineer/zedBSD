<!-- awesome-plan project=zedbsd record=ws288-p019 -->

# ws288-p019 zedBSD の `vk/`（Vulkan の executor = 検証する中継）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6.7（D/R/R'/X、wire の reader、返事と reply thread、session）、§2.4.4（gpu-op の表）、§6.4（drv_gpu との結び: capset・jobs・command）、§8 の p019 の行。
- 状態: in-progress（2026-10-25 08:46 q1569 P3）
- 依存: p006（driver、cleared: capset.c・jobs.c は build と host 試験だけで未結線）、p010（compiler、cleared）、p017・p018（firmware の `GHCI_*` の形。p018 は P2 が並行で実装中: GHCI_HEAP 0x10108、record の時の µop、`include/ghci_layout.h`、scratch の kind 14）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §8 の p019 の行、ZYBO を除く）

- `src/drivers/gpu/ghci/vk/`: codec の生成（`ghci_vkwire.inc`、firmware と共有）、wire の reader（i915 の `i915_wire_reader` を powervr と同じ形で再利用）、D/R/R'/X の表、driver の帳簿、D の返事（capset・limit・memory requirements・`ghci_layout.h` の写し・fence）、R' の書き換え（`GHCI_MEMORY`、pipeline の作成で stage ごとに compile → `GHCI_PROGRAM`、scratch の kind 14 を含む）、staging と `GHCI_FRAME`、result page と reply thread、jobs の marker、`EXECUTE_STREAMS` の入れ子、session の open/close（`GHCI_HEAP` で heap を貸す）。
- p006 の残り: capset・COMMAND・JOB・NOTIFICATION・SHARE の capability の結線（executor ができたので出す）。
- limit と feature の表は design §15（MSAA）・§16（p030）・p010 の決定（U49 の任意の feature、fragmentStoresAndAtomics など）と GHCI の FMT capability に合わせる。
- host の試験: gpu-op の frame を作って staging と ring の packet の列と返事の blob を比べる。
- ZYBO の実 driver の試験は対象外（ZYBO は外した）。Verilator の端から端は p011、実機は p028。

## 受け入れ

host の試験が PASS（staging の列と返事の blob の形が libvulkan の読む形）、amd64 の vmunix（`plan/ws288/tests/config-amd64-ghci.mk`）が warning 0、`plan/coding-style.md` の全文に合う。

## 記録

- 2026-10-25 08:46 Q1: 新設、P3 に（q1569）。
- 2026-10-25 08:59 Q1: 範囲に追加（ユーザーの問い「scanoutはバッファをコピーせずにレンダリングターゲットをそのまま出力できますか？」への Q1 の答えから）: **copy しない flip**。swapchain の image を DC の条件（BGRA8・RGBA8・RGB565、1 sample、base と行の揃え、同じ大きさ・stride・format）に合わせて driver が確保し、present は FB_NEXT に address を書いて FLIP_DONE を待つ。合わない buffer だけ今の BLT の copy。DC の割り込みが PCIe の MSI に届くかを GHCI §13・§15.2 に書く。
